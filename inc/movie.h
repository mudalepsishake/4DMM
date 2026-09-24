/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    Status: All changes must be code reviewed.

    Movie Stuff

        A single view on a movie (MVU)

                DDG  	--->	MVU

        Callbacks to client (MCC)

                BASE 	--->	MCC

        A single movie (MVIE)

                DOCB	--->	MVIE

***************************************************************************/

#ifndef MOVIE_H
#define MOVIE_H

//
// 3DMMv1.0: Tools that can be "loaded" on the mouse cursor
//
enum
{
    toolPlace, // 3DMMv1.0: (to position an actor when you first add it)
    toolCompose,
    toolAction, // 3DMMv1.0: action is in _anidTool if we're motion filling
    toolTweak,
    toolRotateX,
    toolRotateY,
    toolRotateZ,
    toolCostumeCmid, // 3DMMv1.0: cmid is in _cmidTool
    toolSquashStretch,
    toolSoonerLater,
    toolResize,
    toolNormalizeRot,
    toolCopyObject,
    toolCutObject,
    toolPasteObject,
    toolCopyRte,
    toolDefault,
    toolTboxMove,
    toolTboxUpDown,
    toolTboxLeftRight,
    toolTboxFalling,
    toolTboxRising,
    toolSceneNuke,
    toolIBeam,
    toolSceneChop,
    toolSceneChopBack,
    toolCutText,
    toolCopyText,
    toolPasteText,
    toolPasteRte,
    toolActorEasel,
    toolActorSelect,
    toolActorNuke,
    toolTboxPaintText,
    toolTboxFillBkgd,
    toolTboxStory,
    toolTboxCredit,
    toolNormalizeSize,
    toolRecordSameAction,
    toolSounder,
    toolLooper,
    toolMatcher,
    toolTboxFont,
    toolTboxStyle,
    toolTboxSize,
    toolListener,
    toolAddAFrame,
    toolFWAFrame,
    toolRWAFrame,
    toolComposeAll,
    toolUndo,     // 3DMMv1.0: For playing UI Sound only
    toolRedo,     // 3DMMv1.0: For playing UI Sound only
    toolFWAScene, // 3DMMv1.0: For playing UI Sound only
    toolRWAScene, // 3DMMv1.0: For playing UI Sound only

    toolLimMvie
};

//
// 3DMMv1.0: Used to tell the client the change the state of the undo UI
//
enum
{
    undoDisabled = 1,
    undoUndo,
    undoRedo
};

//
// 3DMMv1.0: grfbrws flags
//
enum
{
    fbrwsNil = 0,
    fbrwsProp = 1, // 3DMMv1.0: fTrue implies prop or 3d
    fbrwsTdt = 2,  // 3DMMv1.0: fTrue means this is a 3-D Text object
    fbrwsVxp = 4   // 4DMM: movie-owned template imported natively from a VXP
};

//
//
// 3DMMv1.0: A class for handling a single Movie view
//
//

//
// 3DMMv1.0: The class definition
//
#define MVU_PAR DDG

typedef class MVU *PMVU;
#define kclsMVU KLCONST3('M', 'V', 'U')
class MVU : public MVU_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(MVU)

  protected:
    /* 3DMMv1.0: Make these static; we want to be able to set and restore without having
        an actual MVU, and they shouldn't be getting set per-MVU anyway */
    static bool _fKbdDelayed;     // 3DMMv1.0: fTrue == we have delayed the keyboard
    static int32_t _dtsKbdDelay;  // 3DMMv1.0: System keyboard delay before first repeat
    static int32_t _dtsKbdRepeat; // 3DMMv1.0: System keyboard delay between repeats

    int32_t _dxp;
    int32_t _dyp; // 3DMMv1.0: width and height rendered area.

    bool _fTrackingMouse; // 3DMMv1.0: Is the mouse currently being tracked?
    int32_t _xpPrev;      // 3DMMv1.0: X location of the mouse.
    int32_t _ypPrev;      // 3DMMv1.0: Y location of the mouse.
    BRS _dzrPrev;         // 3DMMv1.0: Z motion of the "mouse" (arrow keys)
    int32_t _grfcust;     // 3DMMv1.0: Options in effect when mouse was down.
    PCURS _pcursDefault;  // 3DMMv1.0: Default cursor for when a tool is not applicable.
    PACTR _pactrListener; // 3DMMv1.0: Pactr of the actor being auditioned
    PACTR _pactrRestore;  // 3DMMv1.0: Restore for actor recording

    int32_t _anidTool;  // 3DMMv1.0: Current selected action
    TAG _tagTool;       // 3DMMv1.0: Tag associated with current tool.
    PTMPL _ptmplTool;   // 3DMMv1.0: Template associated with the current tool.
    int32_t _cmidTool;  // 3DMMv1.0: Costume id associated with current tool.
    bool _fCyclingCels; // 3DMMv1.0: Are we cycling cels in toolRecord?
    int32_t _tool;      // 3DMMv1.0: Current tool loaded on cursor

    // 3DMMv1.0: REVIEW Seanse(SeanSe): MVIE should not be creating/mucking with actor undo
    // 3DMMv1.0:   objects.  V2.666 should revisit this issue and see if we can get ACTR to
    // 3DMMv1.0:   do all its own undo objects (e.g. Growing over a long drag could become
    // 3DMMv1.0:   a StartGrow/Grow/EndGrow sequence).  This also effects _pactrRestore.
    PAUND _paund;           // 3DMMv1.0: Actor undo object to save from mouse down to drag.
    uint32_t _tsLast;       // 3DMMv1.0: Last time a cell was recorded.
    uint32_t _tsLastSample; // 3DMMv1.0: Last time mouse/kbd sampled
    RC _rcFrame;            // 3DMMv1.0: Frame for creating a text box.

    BRS _rgrAxis[3][3];   // 3DMMv1.0: Conversion from mouse points to 3D points.
    bool _fRecordDefault; // 3DMMv1.0: fTrue = Record; fFalse = Rerecord

    bool _fPause : 1;             // 3DMMv1.0: fTrue if pausing play until a click.
    bool _fTextMode : 1;          // 3DMMv1.0: fTrue if Text boxes are active.
    bool _fRespectGround : 1;     // 3DMMv1.0: fTrue if Y=0 is enforced.
    bool _fContinueInPlace : 1;   // Continue action while cancelling route displacement.
    bool _fManualCameraMousePrimed : 1;
    bool _fManualCameraCaptured : 1;
    bool _fManualCameraFly : 1;
    bool _fManualCameraShiftDown : 1;
    bool _fManualCameraCtrlDown : 1;
    bool _fManualCameraCtrlChordUsed : 1;
    bool _fManualCameraDDown : 1;
    bool _fManualCameraRDown : 1;
    bool _fManualCameraEnterDown : 1;
    bool _fManualCameraEDown : 1;
    bool _fManualCameraLeftDown : 1;
    bool _fManualCameraRightDown : 1;
    bool _fFreeLookFDown : 1;
    bool _fFreeLookLButtonDown : 1;
    bool _fDeselectEscapeDown : 1; // edge latch for ESC deselect when cursor is visible
    bool _fMultiDeselectClick : 1; // Ctrl-click under -multi removes one selected object without dragging it
    bool _fSetFRecordDefault : 1; // 3DMMv1.0: fTrue if using hotkeys to rerecord.
    bool _fMouseOn : 1;
    bool _fEntireScene : 1; // 3DMMv1.0: Does positioning effect the entire scene?

    bool _fMouseDownSeen; // 3DMMv1.0: Was the mouse depressed during a place.
    PACTR _pactrUndo;     // 3DMMv1.0: Actor to use for undo object when roll-calling.

    ACR _acr;            // 3DMMv1.0: Color for painting text.
    int32_t _onn;        // 3DMMv1.0: Font for text
    int32_t _dypFont;    // 3DMMv1.0: Font size for text
    uint32_t _grfont;    // 3DMMv1.0: Font style for text
    int32_t _lwLastTime; // 3DMMv1.0: State variable for the last time through.
    int32_t _xpManualCameraPrev;
    int32_t _ypManualCameraPrev;
    uint32_t _tsManualCameraLast;

    MVU(PDOCB pdocb, PGCB pgcb) : DDG(pdocb, pgcb)
    {
    }

    //
    // 3DMMv1.0: Clipboard support
    //
    bool _FCopySel(PDOCB *ppdocb, bool fRteOnly);
    void _ClearSel(void) override;
    bool _FPaste(PCLIP pclip);

    void _PositionActr(BRS dxrWld, BRS dyrWld, BRS dzrWld);
    void _MouseDown(CMD_MOUSE *pcmd);
    void _MouseDrag(CMD_MOUSE *pcmd);
    void _MouseUp(CMD_MOUSE *pcmd);

    void _ActorClicked(PACTR pactr, bool fDown);

  public:
    static void SlowKeyboardRepeat(void);
    static void RestoreKeyboardRepeat(void);

    //
    // 3DMMv1.0: Constructors and desctructors
    //
    static MVU *PmvuNew(PMVIE pmvie, PGCB pgcb, int32_t dxy, int32_t dyp);
    ~MVU(void);

    //
    // 3DMMv1.0: Accessor for getting the owning movie
    //
    PMVIE Pmvie()
    {
        return (PMVIE)_pdocb;
    }

    //
    // 3DMMv1.0: Command handlers
    //
    virtual bool FCmdTrackMouse(PCMD_MOUSE pcmd) override;
    virtual bool FCmdMouseMove(PCMD_MOUSE pcmd) override;
    virtual bool FCmdClip(CMD *pcmd) override;
    virtual bool FCmdUndo(PCMD pcmd) override;
    virtual bool FCloseDoc(bool fAssumeYes, bool fSaveDDG = fFalse);
    virtual bool FCmdSave(PCMD pcmd) override;
    bool FDoClip(int32_t tool);
    bool FCmdIdle(CMD *pcmd); // 3DMMv1.0: Called whenever an idle loop is seen.
    bool FCmdRollOff(CMD *pcmd);
    bool FCmdLightLab(CMD *pcmd);
    void BeginManualCameraInput(void);
    void EndManualCameraInput(void);
    void ResetManualCameraInput(void);

    //
    // 3DMMv1.0: View specific functions.
    //
    void SetTool(int32_t tool);
    int32_t Tool(void)
    {
        return _tool;
    }
    int32_t AnidTool(void)
    {
        return _anidTool;
    }
    int32_t CmidTool(void)
    {
        return _cmidTool;
    }
    PTAG PtagTool(void)
    {
        return &_tagTool;
    }
    void SetAnidTool(int32_t anid)
    {
        _anidTool = anid;
    }
    void SetTagTool(PTAG ptag);
    void SetCmidTool(int32_t cmid)
    {
        _cmidTool = cmid;
    }
    void StartPlaceActor(bool fEntireScene = fFalse);
    void EndPlaceActor(void);
    void WarpCursToCenter(void);
    void WarpCursToActor(PACTR pactr);
    void AdjustCursor(int32_t xp, int32_t yp);
    void MouseToWorld(BRS dxrMouse, BRS dyrMouse, BRS dzrMouse, BRS *pdxrWld, BRS *pdyrWld, BRS *pdzrWld, bool fRecord);
    void SetAxis(BRS rgrAxis[3][3])
    {
        BltPb(rgrAxis, _rgrAxis, SIZEOF(BRS) * 9);
    }
    void SetFRecordDefault(bool f)
    {
        _fRecordDefault = f;
    }
    void SetFRespectGround(bool f)
    {
        _fRespectGround = f;
    }
    void SetFContinueInPlace(bool f)
    {
        _fContinueInPlace = f;
    }
    bool FContinueInPlace(void)
    {
        return _fContinueInPlace;
    }
    bool FRecordDefault()
    {
        return _fRecordDefault;
    }
    void PauseUntilClick(bool fPause)
    {
        _fPause = fPause;
    }
    bool FPausing(void)
    {
        return _fPause;
    }
    bool FTextMode(void)
    {
        return _fTextMode;
    }
    bool FActrMode(void)
    {
        return !_fTextMode;
    }
    bool FRespectGround(void)
    {
        return _fRespectGround;
    }
    void SetActrUndo(PACTR pactr)
    {
        _pactrUndo = pactr;
    }
    void SetPaintAcr(ACR acr)
    {
        _acr = acr;
    }
    ACR AcrPaint(void)
    {
        return _acr;
    }
    void SetOnnTextCur(int32_t onn)
    {
        _onn = onn;
    }
    int32_t OnnTextCur(void)
    {
        return _onn;
    }
    void SetDypFontTextCur(int32_t dypFont)
    {
        _dypFont = dypFont;
    }
    int32_t DypFontTextCur(void)
    {
        return _dypFont;
    }
    void SetStyleTextCur(uint32_t grfont)
    {
        _grfont = grfont;
    }
    uint32_t GrfontStyleTextCur(void)
    {
        return _grfont;
    }

    //
    // 3DMMv1.0: Routines for communicating with the framework
    //
    void Draw(PGNV pgnv, RC *prcClip) override;
};

//
//
// 3DMMv1.0: Movie Client Callbacks.  Used for filling in
// 3DMMv1.0: parameters for this movie, and for notifying
// 3DMMv1.0: client of state changes.
//
//

#define MCC_PAR BASE

class MCC;

typedef MCC *PMCC;
#define kclsMCC KLCONST3('M', 'C', 'C')
class MCC : public MCC_PAR
{
  protected:
    int32_t _dxp;
    int32_t _dyp;
    int32_t _cbCache;

  public:
    MCC(int32_t dxp, int32_t dyp, int32_t cbCache)
    {
        _dxp = dxp;
        _dyp = dyp;
        _cbCache = cbCache;
    }
    virtual int32_t Dxp(void)
    {
        return _dxp;
    } // 3DMMv1.0: Width of the rendering area
    virtual int32_t Dyp(void)
    {
        return _dyp;
    } // 3DMMv1.0: Height of the rendering area
    virtual int32_t CbCache(void)
    {
        return _cbCache;
    } // 3DMMv1.0: Number of bytes to use for caching.
    virtual void SetCurs(int32_t tool)
    {
    } // 3DMMv1.0: Sets the cursor based on the tool, may be pvNil.
    virtual void UpdateRollCall(void)
    {
    } // 3DMMv1.0: Tells the client to update its roll call.
    virtual void UpdateAction(void)
    {
    } // 3DMMv1.0: Tells the client to update its action menu.
    virtual void UpdateScrollbars(void)
    {
    } // 3DMMv1.0: Tells the client to update its scrollbars.
    virtual void PlayStopped(void)
    {
    } // 3DMMv1.0: Tells the client that playback was stopped internally.
    virtual void ChangeTool(int32_t tool)
    {
    } // 3DMMv1.0: Tells the client that the tool was changed internally.
    virtual void SceneNuked(void)
    {
    } // 3DMMv1.0: Tells the client that a scene was nuked.
    virtual void SceneUnnuked(void)
    {
    } // 3DMMv1.0: Tells the client that a scene was nuked and now undone.
    virtual void ActorNuked(void)
    {
    } // 3DMMv1.0: Tells the client that an actor was nuked.
    virtual void EnableActorTools(void)
    {
    } // 3DMMv1.0: Tells the client that the first actor was added.
    virtual void EnableTboxTools(void)
    {
    } // 3DMMv1.0: Tells the client that the first textbox was added.
    virtual void TboxSelected(void)
    {
    } // 3DMMv1.0: Tells the client that a new text box was selected.
    virtual void ActorSelected(int32_t arid)
    {
    } // 3DMMv1.0: Tells the client that an actor was selected
    virtual void ActorEasel(bool *pfActrChanged)
    {
    } // 3DMMv1.0: Lets client edit 3-D Text or costume
    virtual void SetUndo(int32_t undo)
    {
    } // 3DMMv1.0: Tells the client the state of the undo buffer.
    virtual void SceneChange(void)
    {
    } // 3DMMv1.0: Tells the client that a different scene is the current one
    virtual void PauseType(WIT wit)
    {
    } // 3DMMv1.0: Tells the client of a new pause type for this frame.
    virtual void Recording(bool fRecording, bool fRecord)
    {
    } // 3DMMv1.0: Tells the client that the movie engine is recording or not.
    virtual void StartSoonerLater(void)
    {
    } // 3DMMv1.0: Tells the client that an actor is selected for sooner/latering.
    virtual void EndSoonerLater(void)
    {
    } // 3DMMv1.0: Tells the client that an actor is done for sooner/latering.
    virtual void NewActor(void)
    {
    } // 3DMMv1.0: Tells the client that an actor has just been placed.
    virtual void StartActionBrowser(void)
    {
    } // 3DMMv1.0: Tells the client to start up the action browser.
    virtual void StartListenerEasel(void)
    {
    } // 3DMMv1.0: Tells the client to start up the listener easel.
    virtual bool GetFniSave(FNI *pfni, int32_t lFilterLabel, int32_t lFilterExt, int32_t lTitle, PCSZ lpstrDefExt,
                            PSTN pstnDefFileName)
    {
        return fFalse;
    } // 3DMMv1.0: Tells the client to start up the save portfolio.
    virtual void PlayUISound(int32_t tool, int32_t grfcust = 0)
    {
    } // 3DMMv1.0: Tells the client to play sound associated with use of tool.
    virtual void StopUISound(void)
    {
    } // 3DMMv1.0: Tells the client to stop sound associated with use of tools.
    virtual void UpdateTitle(PSTN pstnTitle)
    {
    } // 3DMMv1.0: Tells the client that the movie name has changed.
    virtual void EnableAccel(void)
    {
    } // 3DMMv1.0: Tells the client to enable keyboard accelerators.
    virtual void DisableAccel(void)
    {
    } // 3DMMv1.0: Tells the client to disable keyboard accelerators.
    virtual void GetStn(int32_t ids, PSTN pstn)
    {
    } // 3DMMv1.0: Requests the client to fetch the given ids string.
    virtual int32_t DypTextDef(void)
    {
        return vpappb->DypTextDef();
    }
    virtual int32_t DypTboxDef(void)
    {
        return 14;
    }
    virtual void SetSndFrame(bool fSoundInFrame)
    {
    }
    virtual bool FMinimized(void)
    {
        return fFalse;
    }
    virtual bool FQueryPurgeSounds(void)
    {
        return fFalse;
    }
};

/* 3DMMv1.0: A SCENe Descriptor */
typedef struct _scend
{
    /* 3DMMv1.0: The first fields are private...the client shouldn't change them, and
        in fact, generally shouldn't even look at them */
    int32_t imvied;  // 3DMMv1.0: index of the MVIED for this scene
    CNO cno;         // 3DMMv1.0: the CNO of this scene chunk
    CHID chid;       // 3DMMv1.0: the original CHID
    PMBMP pmbmp;     // 3DMMv1.0: pointer to thumbnail MBMP
                     /* 3DMMv1.0: The client can read or write the following fields */
    TRANS trans;     // 3DMMv1.0: the transition that will occur after this scene
    bool fNuked : 1; // 3DMMv1.0: fTrue if this scene has been deleted
} SCEND, *PSCEND;

/* 3DMMv1.0: A MoVIE Descriptor */
typedef struct _mvied
{
    PCRF pcrf;       // 3DMMv1.0: the file this scene's movie is in
    CNO cno;         // 3DMMv1.0: CNO of the MVIE chunk
    int32_t aridLim; // 3DMMv1.0: _aridLim from the MVIE
} MVIED, *PMVIED;

/* 3DMMv1.0: A Composite MoVIe */
typedef struct _cmvi
{
    PGL pglmvied; // 3DMMv1.0: GL of movie descriptors
    PGL pglscend; // 3DMMv1.0: GL of scene descriptors

    void Empty(void);
#ifdef DEBUG
    void MarkMem(void);
#endif
} CMVI, *PCMVI;

//
//
// 3DMMv1.0: Movie Class.
//
//

const int32_t kccamMax = 9;

// One complete scene-local depth-motion tween.  A scene may contain many
// disjoint tweens.  Frames use the one-based Scene-tab numbering shown to the
// user; id is stable only within the loaded movie and joins editor operations
// to the correct tween after sorting.
struct CTTWEEN
{
    int32_t iscen;
    int32_t id;
    int32_t nfrmFirst;
    int32_t nfrmLast;
    float zFirst;
    float zLast;

    // Complete camera pose where the Z-only tween begins.  zFirst/zLast are
    // travel distances from this pose along the tween's yaw-oriented local Z
    // axis, not absolute scene coordinates.
    float x;
    float y;
    float z;
    float pitch;
    float yaw;
};

// One manually positioned camera sample.  Values are scene-baseline-relative
// offsets and rotations at a one-based Studio frame number.
struct CTMAN
{
    int32_t iscen;
    int32_t nfrm;
    float x;
    float y;
    float z;
    float pitch;
    float yaw;
    float roll; // currently authored only by transient Free Look Q/E

    // Structural hold keys preserve camera continuity when timeline frames are
    // inserted.  They participate in interpolation and are written to .3ct,
    // but they are not user-authored Manual Camera frames and therefore do not
    // light the Manual Camera toggle or open in its editor.
    bool fImplicit;
};

const int32_t kcctweenMax = 256;
const int32_t kcctmanMax = 4096;

// 4DMM Light Lab sidecar records.  Lights are scene-local and bind to an
// actor/prop ARID, so multiple independently configured light objects can
// coexist without changing the .3mm chunk format.
// actorlight29: 1995 BRender shipped with a 16-light fixed table.  Source
// BRender is patched to the same larger value at configure time, removing the
// tiny Light Lab ceiling while keeping the old fixed-layout code simple and
// deterministic.  256 is intentionally a practical "unlimited" ceiling for
// 3DMM authoring rather than pretending a finite renderer can be literally
// unbounded.
const int32_t kclightLabMax = 256;
const int32_t kc4DMMSceneSettingsMax = 4096;
const int32_t kc4DMMNonSelectableMax = 4096;
const int32_t kc4DMMObjectPropertiesMax = 4096;

// Per-object 4DMM render properties persisted in the .3ct sidecar.  Missing
// records use the stock defaults: ordinary shadow casting and current/default
// shadow receiver filtering.
struct OBJECTPROPERTIES
{
    int32_t iscen;
    int32_t arid;
    bool fFlushOverlap;
    bool fCastShadows;
};

// -multi Object Groups are persisted in the .3ct sidecar so the original
// .3mm document format remains untouched.  Membership references normal
// scene ACTR ARIDs, which covers actors, props and 3D Words uniformly.
const int32_t kcObjectGroupMax = 512;
const int32_t kcObjectGroupMemberMax = 8192;
const int32_t kcchObjectGroupName = 64;
struct OBJECTGROUP
{
    int32_t id;
    int32_t iscen;
    bool fLocked;
    achar szName[kcchObjectGroupName];

    // Runtime-only shared BRender transform authority. This actor is never
    // serialized to .3ct; it is rebuilt from the immutable bind pose plus the
    // live .3mm actor state whenever the scene is opened.
    PBACT pbactParent;
};

struct OBJECTGROUPMEMBER
{
    int32_t idGroup;
    int32_t arid;
    // Immutable bind-space reference pose. The live .3mm actor events carry
    // later group translation/rotation; keeping this pose fixed lets the
    // complete group be reconstructed as one rigid coordinate frame.
    BRS xr;
    BRS yr;
    BRS zr;
    BMAT34 bmat34;

    // Runtime-only complete BODY bind-local matrix. Unlike bmat34 above,
    // this includes the member's rest orientation plus any current
    // stretch/squash and grow/shrink state. It is captured from the live
    // BODY after reparenting and is never serialized to .3ct.
    BMAT34 bmat34BodyLocal;
    bool fBodyLocalValid;
};

// Logical Object Group snapshot used by undo/redo of membership edits.
// Runtime BRender parent pointers and frozen BODY-local matrices are deliberately
// excluded when the snapshot is captured; they are rebuilt from the restored
// logical group records and current ACTR state.
struct OBJECTGROUPSTATE
{
    OBJECTGROUP rgObjectGroup[kcObjectGroupMax];
    int32_t cObjectGroup;
    OBJECTGROUPMEMBER rgObjectGroupMember[kcObjectGroupMemberMax];
    int32_t cObjectGroupMember;
    int32_t idObjectGroupNext;
};

// Actor Studio / Create Part metadata.  The geometry for a future custom
// actor is still authored through normal 3DMM/BRender objects; these records
// describe the user-owned object/part graph and custom animation ownership.
// They live in the 4DMM .3ct metadata and are embedded in .vmm packages.
const int32_t kc4DMMCustomObjectMax = 256;
const int32_t kc4DMMCustomPartMax = 2048;
const int32_t kc4DMMCustomActionMax = 2048;
const int32_t kcch4DMMCustomName = 64;
enum CUSTOMTEMPLATEKIND
{
    kctkNone = 0,
    kctkDefault = 1,
    kctkCustomObject = 2
};
struct CUSTOMOBJECT
{
    int32_t id;
    bool fProp;
    int32_t templateKind;
    int32_t idTemplateCustom;
    int32_t sidTemplate;
    CTG ctgTemplate;
    CNO cnoTemplate;
    // Native writable TMPL owned by this VMM, when one exists.  Create-Part
    // definitions may remain metadata-only until their BODY synthesis step.
    CNO cnoOwnedTmpl;
    achar szName[kcch4DMMCustomName];
};
struct CUSTOMPART
{
    int32_t idObject;
    int32_t iscen;
    int32_t idGroup;

    // v2 Actor Studio provenance. One Create Part/Add-to-object operation has
    // one idImport; each source Object contributes one contiguous BODY-part
    // range. Older v1 records leave these fields zero and display flat.
    int32_t idImport;
    int32_t aridSource;
    int32_t ipartFirst;
    int32_t cpart;
    // v3 provenance: the source Object was a 3D Word/TDT. The baked custom
    // BODY is no longer itself a TDT, so Actor Studio needs this bit when a
    // tool intentionally applies only to imported 3D Word geometry.
    bool fSourceTdt;
    achar szGroupName[kcch4DMMCustomName];
    achar szObjectName[kcch4DMMCustomName];
};
struct CUSTOMACTION
{
    CNO cnoTmpl;
    int32_t anid;
    achar szName[kcch4DMMCustomName];
};

// Actor Studio frame clipboard/edit payload. A frame is stored in resolved
// form rather than as raw CPS matrix indexes so it can be pasted into another
// action whose GLXF has a completely different index space.
const int32_t kc4DMMActorStudioFramePartMax = 512;
struct ACTORSTUDIOFRAMESNAPSHOT
{
    bool fValid;
    CNO cnoTmplSource;
    int32_t anidSource;
    CEL cel;
    int32_t cpart;
    CPS rgcps[kc4DMMActorStudioFramePartMax];
    BMAT34 rgbmat34[kc4DMMActorStudioFramePartMax];
};
enum ACTORSTUDIOFRAMEWRITE
{
    kasfwReplace = 1,
    kasfwInsertBefore = 2,
    kasfwInsertAfter = 3
};

// Actor Studio virtual-tree selection level.  Keep these values aligned with
// the native ASW tree rows so engine-side topology edits can preserve Object
// and Object Group provenance without depending on studio-only types.
enum ACTORSTUDIOSELECTIONKIND
{
    kasskPart = 1,
    kasskObject = 2,
    kasskGroup = 3,
    kasskDefaultPartGroup = 4,
    kasskNonGroupedParts = 5,
    kasskActorPropRoot = 6
};

// VMM filenames are not guaranteed to arrive with kftgVmm because several
// native/Windows open paths construct an FNI before the legacy FTG registry
// knows about the .vmm suffix.  Treat the actual extension as authoritative.
bool F4DMMPathIsVmm(PSTN pstnPath);
bool F4DMMFniIsVmm(PFNI pfni);

struct LIGHTLAB
{
    int32_t iscen;
    int32_t arid;
    // Scene-relative, one-based lifetime. Spawn is always at least frame 1.
    // Despawn 0 is the open-ended sentinel (the light remains for the rest of
    // the scene, including frames appended later).
    int32_t nfrmSpawn;
    int32_t nfrmDespawn;
    bool fEnabled;
    bool fGenerateShadows;
    bool fAttachmentHideable;
    int32_t intensity;
    float edgeGradient;
    float diameter;
    float range;
    achar szShape[16];
};

// Complete in-memory camera-track snapshot used by scene/frame undo records.
// Camera data lives in the .3ct sidecar, so the ordinary serialized SCEN
// snapshot cannot restore it by itself.
struct CTSTATE
{
    CTTWEEN rgctween[kcctweenMax];
    int32_t cctween;
    CTMAN rgctman[kcctmanMax];
    int32_t cctman;
    LIGHTLAB rglightLab[kclightLabMax];
    int32_t clightLab;
    bool rgfSceneLightsEnabled[kc4DMMSceneSettingsMax];
    bool rgfSceneHideLightObjects[kc4DMMSceneSettingsMax];
    bool rgfSceneDefaultLightingShaders[kc4DMMSceneSettingsMax];
    bool rgfSceneLightLabCombineLegacy[kc4DMMSceneSettingsMax];
    bool fDefaultLightingShaders;
    bool fLightLabCombineLegacy;
    bool fFreeLookAlwaysLastKnown;
    bool fDontAskLightLabCut;
    bool fDontAskShaderPropagation;
    int32_t rgaridNonSelectable[kc4DMMNonSelectableMax];
    int32_t caridNonSelectable;
    OBJECTPROPERTIES rgObjectProperties[kc4DMMObjectPropertiesMax];
    int32_t cObjectProperties;
};

typedef class MVIE *PMVIE;

#define MVIE_PAR DOCB
#define kclsMVIE KLCONST4('M', 'V', 'I', 'E')
class MVIE : public MVIE_PAR
{
    friend class ASDE;
    RTCLASS_DEC
    MARKMEM
    ASSERT
    CMD_MAP_DEC(MVIE)

  protected:
    int32_t _aridLim; // 3DMMv1.0: Highest actor id in use.
    int32_t _arid4DMMActorStudioPreaddedPlacement; // runtime-only roll-call exposure during AS placement

    PCRF _pcrfAutoSave; // 3DMMv1.0: CRF/CFL of auto save file.
    PFIL _pfilSave;     // 3DMMv1.0: User's document

    CNO _cno; // 3DMMv1.0: CNO of movie in current file.

    STN _stnTitle; // 3DMMv1.0: Title of the movie

    PGST _pgstmactr;             // 3DMMv1.0: GST of actors in the movie (for roll call)
    PSCEN _pscenOpen;            // 3DMMv1.0: Index of current open scene.
    int32_t _cscen;              // 3DMMv1.0: Number of scenes in the movie.
    int32_t _iscen;              // 3DMMv1.0: Number of scene open in the movie.
    bool _fAutosaveDirty : 1;    // 3DMMv1.0: Is the movie in memory different than disk
    bool _fFniSaveValid : 1;     // 3DMMv1.0: Does _fniSave contain a file name.
    bool _fPlaying : 1;          // 3DMMv1.0: Is the movie playing?
    bool _fScrolling : 1;        // 3DMMv1.0: During playback only, are we scrolling textboxes
    bool _fPausing : 1;          // 3DMMv1.0: Are we executing a pause.
    bool _fIdleSeen : 1;         // 3DMMv1.0: fTrue if we have seen an idle since this was cleared.
    bool _fStopPlaying : 1;      // 3DMMv1.0: Should we stop the movie playing
    bool _fSoundsEnabled : 1;    // 3DMMv1.0: Should we play sounds or not.
    bool _fOldSoundsEnabled : 1; // 3DMMv1.0: Old value of above.
    bool _fDocClosing : 1;       // 3DMMv1.0: Flags doc is to be closed
    bool _fGCSndsOnClose : 1;    // 3DMMv1.0: Garbage collection of sounds on close
    bool _fReadOnly : 1;         // 3DMMv1.0: Is the original file read-only?
    bool _fTestLightActive : 1;  // one or more -l/Light Lab lights are installed in BWLD

    PBWLD _pbwld;      // 3DMMv1.0: The brender world for this movie
    BACT _bactTestLight; // fallback scene-local laboratory light when no object lights exist
    BLIT _blitTestLight; // BRender data owned by _bactTestLight
    LIGHTLAB _rglightLab[kclightLabMax];
    int32_t _clightLab;
    BACT _rgbactLightLab[kclightLabMax];
    BLIT _rgblitLightLab[kclightLabMax];
    bool _rgfLightLabActive[kclightLabMax];
    bool _rgfSceneLightsEnabled[kc4DMMSceneSettingsMax];
    bool _rgfSceneHideLightObjects[kc4DMMSceneSettingsMax];
    bool _rgfSceneDefaultLightingShaders[kc4DMMSceneSettingsMax];
    bool _rgfSceneLightLabCombineLegacy[kc4DMMSceneSettingsMax];
    int32_t _rgaridNonSelectable[kc4DMMNonSelectableMax];
    int32_t _caridNonSelectable;
    OBJECTPROPERTIES _rgObjectProperties[kc4DMMObjectPropertiesMax];
    int32_t _cObjectProperties;
    OBJECTGROUP _rgObjectGroup[kcObjectGroupMax];
    int32_t _cObjectGroup;
    OBJECTGROUPMEMBER _rgObjectGroupMember[kcObjectGroupMemberMax];
    int32_t _cObjectGroupMember;
    int32_t _idObjectGroupNext;
    CUSTOMOBJECT _rg4DMMCustomObject[kc4DMMCustomObjectMax];
    int32_t _c4DMMCustomObject;
    int32_t _id4DMMCustomObjectNext;
    CUSTOMPART _rg4DMMCustomPart[kc4DMMCustomPartMax];
    int32_t _c4DMMCustomPart;
    CUSTOMACTION _rg4DMMCustomAction[kc4DMMCustomActionMax];
    int32_t _c4DMMCustomAction;
    bool _fRequiresVmmSave;
    bool _fDefaultLightingShaders;
    bool _fLightLabCombineLegacy;
    bool _fFreeLookAlwaysLastKnown;
    bool _fDontAskLightLabCut;
    bool _fDontAskShaderPropagation;
    bool _fDefaultMusicForNewScenes;
    bool _fExperimental100xGrow;
    bool _fExperimental100xShrink;
    float _flCameraMoveSpeed;
    float _flCameraMouseSensitivity;
    int32_t _rgaridHiddenLightObjects[kclightLabMax];
    int32_t _caridHiddenLightObjects;
    PMSQ _pmsq;        // 3DMMv1.0: Message Sound Queue
    CLOK _clok;        // 3DMMv1.0: Clock for playing the film
    uint32_t _tsStart; // 3DMMv1.0: Time last play started.
    int32_t _cnfrm;    // 3DMMv1.0: Number of frames since last play started.

    PMCC _pmcc; // 3DMMv1.0: Parameters and callbacks.

    WIT _wit;     // 3DMMv1.0: Pausing type
    int32_t _dts; // 3DMMv1.0: Number of clock ticks to pause.
    TRANS _trans; // 3DMMv1.0: Transition type to execute.

    int32_t _vlmOrg; // 3DMMv1.0: original SNDM volume, before fadeout, if we are done with fadeout, then 0

#ifdef DEBUG
    bool _fWriteBmps;
    int32_t _lwBmp;
#endif // 3DMMv1.0: DEBUG

    PGL _pglclrThumbPalette; // 3DMMv1.0: Palette to use for thumbnail rendering.

    // Camera tracks are sidecar data, not part of the .3mm document yet.
    CTTWEEN _rgctween[kcctweenMax];
    int32_t _cctween;
    CTMAN _rgctman[kcctmanMax];
    int32_t _cctman;
    bool _fManualCameraMode : 1;
    bool _fManualCameraRecording : 1;
    bool _fManualCameraRecordLiveValid : 1;
    bool _fManualCameraRecordExitMode : 1;
    bool _fManualCameraEditValid : 1;
    bool _fManualCameraEditHadSample : 1;
    bool _fFreeLookMode : 1;
    bool _fFreeLookOverride : 1;
    bool _fBrowserCameraFollow : 1;
    CTMAN _ctmanManualCameraRecordLive;
    CTMAN _ctmanManualCameraEditStart;
    CTMAN _ctmanManualCameraEditLive;
    int32_t _iscenManualCameraEdit;
    int32_t _nfrmManualCameraEdit;
    CTMAN _ctmanFreeLookStart;
    CTMAN _ctmanFreeLookLive;
    int32_t _iscenFreeLook;
    int32_t _nfrmFreeLook;
    int32_t _aridBrowserCameraFollow;
    int32_t _iscenManualCameraUndo;
    int32_t _nfrmManualCameraUndo;
    STN _stnCameraTrackPath;

  private:
    MVIE(void);
    PTAGL _PtaglFetch(void);                      // 3DMMv1.0: Returns a list of all tags used in movie
    bool _FCloseCurrentScene(void);               // 3DMMv1.0: Closes and releases current scene, if any
    bool _FMakeCrfValid(void);                    // 3DMMv1.0: Makes sure there is a file to work with.
    bool _FUseTempFile(void);                     // 3DMMv1.0: Switches to using a temp file.
    void _MoveChids(CHID chid, bool fDown);       // 3DMMv1.0: Move the chids of scenes in the movie.
    bool _FDoGarbageCollection(PCFL pcfl);        // 3DMMv1.0: Remove unused chunks from movie.
    void _DoSndGarbageCollection(bool fPurgeAll); // 3DMMv1.0: Remove unused user sounds from movie
    bool _FDoMtrlTmplGC(PCFL pcfl);               // 3DMMv1.0: Material and template garbage collection
    CHID _ChidScenNewSnd(void);                   // 3DMMv1.0: Choose an unused chid for a new scene child user sound
    CHID _ChidMvieNewSnd(void);                   // 3DMMv1.0: Choose an unused chid for a new movie child user sound
    void _SetTitle(PFNI pfni = pvNil);            // 3DMMv1.0: Set the title of the movie based on given file name.
    bool _FIsChild(PCFL pcfl, CTG ctg, CNO cno);
    bool _FSetPfilSave(PFNI pfni);
    void _LoadCameraTrack(PFNI pfni);
    void _EnableTestLight(void);
    void _DisableTestLight(void);
    int32_t _ILightLabFind(int32_t iscen, int32_t arid);
    bool _FLightLabActiveAtCurrentFrame(const LIGHTLAB &light) const;
    void _ShowHiddenLightObjects(void);
    void _UpdateHiddenLightObjects(void);
    void _RemoveInvalidLightLabForCurrentScene(void);
    int32_t _ItweenAtFrame(int32_t iscen, int32_t nfrm);
    int32_t _ItweenFindById(int32_t id);
    int32_t _ItweenNewId(void);
    bool _FGetCameraTrackZ(int32_t iscen, int32_t nfrm, BRS *pzrOffset);
    bool _FGetCameraTrackYaw(int32_t iscen, int32_t nfrm, float *pyaw);
    bool _FGetDepthTweenPose(const CTTWEEN &ctween, int32_t nfrm, CTMAN *pctman);
    bool _FSetCameraTrackYaw(int32_t itween, float yaw);
    bool _FFrameCameraOccupied(int32_t iscen, int32_t nfrm, int32_t itweenIgnore = ivNil);
    bool _FEnsureManualCameraFrameUndo(void);
    bool _FBeginManualCameraEdit(void);
    void _ClearManualCameraEdit(void);
    bool _FGetManualCameraState(int32_t iscen, int32_t nfrm, CTMAN *pctman);
    bool _FGetPersistentCameraTrackPose(int32_t iscen, int32_t nfrm, CTMAN *pctman);
    bool _FGetCameraControlState(int32_t iscen, int32_t nfrm, CTMAN *pctman);
    void _ClearFreeLookState(void);
    bool _FCaptureManualCameraFrame(void);
    void _FinishManualCameraRecording(void);
    CTMAN *_PctmanFind(int32_t iscen, int32_t nfrm, bool fCreate);
    void _SortCameraTrackTweens(void);
    bool _FWriteCameraTrack(void);
    void _WarmMovieCache(void);                  // Prime scene resources before the movie becomes visible.
    bool _FEnsureObjectGroupRenderParent(int32_t idGroup, const BMAT34 *pbmat34Parent);
    void _DestroyObjectGroupRenderParent(int32_t idGroup);
    void _DestroyObjectGroupRenderParents(void);
    void _RebuildObjectGroupRenderParentsForCurrentScene(void);

  public:
    //
    // 3DMMv1.0: Begin client useable functions
    //

#ifdef DEBUG
    void SetFWriteBmps(bool fWriteBmps)
    {
        if (fWriteBmps && !_fWriteBmps)
            _lwBmp = 0;
        _fWriteBmps = fWriteBmps;
    }
    bool FWriteBmps(void)
    {
        return _fWriteBmps;
    }
#endif // 3DMMv1.0: DEBUG

    //
    // 3DMMv1.0: Getting views
    //
    PMVU PmvuCur(void);
    PMVU PmvuFirst(void);

    //
    // 3DMMv1.0: Create and Destroy
    //
    // Process-wide launch mode set by APP::_ParseCommandLine.  This avoids
    // relying on the script property table before the movie is constructed.
    static void SetExtendedUndoMode(bool fEnable);
    static bool FExtendedUndoMode(void);
    static void SetMultiSelectMode(bool fEnable);
    static bool FMultiSelectMode(void);
    int32_t CObjectGroups(void) const { return _cObjectGroup; }
    const OBJECTGROUP *PObjectGroup(int32_t iGroup) const
    {
        return FIn(iGroup, 0, _cObjectGroup) ? &_rgObjectGroup[iGroup] : pvNil;
    }
    int32_t CObjectGroupMembers(int32_t idGroup) const;
    const OBJECTGROUPMEMBER *PObjectGroupMember(int32_t idGroup, int32_t iMember) const;
    bool FObjectInObjectGroup(int32_t arid, int32_t *pidGroup = pvNil) const;
    bool FCanBindSelectedObjectGroup(void);
    bool FBindSelectedObjectGroup(int32_t *pidGroup = pvNil);
    bool FCanJoinObjectToGroup(int32_t idGroup, int32_t arid) const;
    bool FJoinObjectToGroup(int32_t idGroup, int32_t arid);
    bool FCanAbandonObjectFromGroup(int32_t idGroup, int32_t arid) const;
    bool FAbandonObjectFromGroup(int32_t idGroup, int32_t arid);
    bool FUnbindObjectGroup(int32_t idGroup);
    bool FGetObjectGroupState(OBJECTGROUPSTATE *pstate) const;
    bool FSwapObjectGroupState(OBJECTGROUPSTATE *pstate);
    bool FRenameObjectGroup(int32_t idGroup, PCSZ pszName);
    bool FSelectObjectGroup(int32_t idGroup);
    bool FGetObjectGroupPivot(int32_t idGroup, BRS *pxr, BRS *pyr, BRS *pzr);
    bool FBeginObjectGroupTransform(int32_t idGroup);
    bool FMoveObjectGroup(int32_t idGroup, BRS dxr, BRS dyr, BRS dzr, bool *pfMoved = pvNil,
                          uint32_t grfmaf = fmafNil);
    bool FScaleObjectGroup(int32_t idGroup, BRS brs, bool fExtended = fFalse);
    bool FSquashStretchObjectGroup(int32_t idGroup, BRS brs);
    bool FRotateObjectGroup(int32_t idGroup, BRA xa, BRA ya, BRA za, bool fFromHereFwd);
    bool FNormalizeObjectGroupRotation(int32_t idGroup);
    bool FNormalizeObjectGroupScale(int32_t idGroup);
    int32_t C4DMMCustomObjects(void) const { return _c4DMMCustomObject; }
    const CUSTOMOBJECT *P4DMMCustomObject(int32_t iObject) const
    {
        return FIn(iObject, 0, _c4DMMCustomObject) ? &_rg4DMMCustomObject[iObject] : pvNil;
    }
    int32_t C4DMMCustomParts(void) const { return _c4DMMCustomPart; }
    const CUSTOMPART *P4DMMCustomPart(int32_t iPart) const
    {
        return FIn(iPart, 0, _c4DMMCustomPart) ? &_rg4DMMCustomPart[iPart] : pvNil;
    }
    bool FCreate4DMMCustomObjectPart(PCSZ pszName, bool fProp, int32_t templateKind,
                                     int32_t idTemplateCustom, int32_t sidTemplate,
                                     CTG ctgTemplate, CNO cnoTemplate, int32_t idGroup);
    bool FCreate4DMMCustomObjectPartFromActor(PCSZ pszName, bool fProp, int32_t templateKind,
                                              int32_t idTemplateCustom, int32_t sidTemplate,
                                              CTG ctgTemplate, CNO cnoTemplate, int32_t arid);
    bool FAdd4DMMCustomPartToObject(int32_t idObject, int32_t idGroup, int32_t aridSource,
                                      bool fUseVacatedModelChids);
    bool FRequiresVmmSave(void) const { return FPure(_fRequiresVmmSave); }
    bool FUseVmmSaveTarget(PFNI pfniVmm);
    bool F4DMMCustomObjectIsHandmade(int32_t iObject) const;
    bool FResolve4DMMReplacementTemplateTag(const TAG *ptagSource, TAG *ptagResolved) const;
    bool FOpen4DMMOwnedTemplateTag(CNO cnoTmpl, TAG *ptag) const;
    bool FPromote4DMMReplacementTemplate(const TAG *ptagSource, const TAG *ptagReplacement,
                                         int32_t aridPrimary);
    bool FActorStudioActionIsCustom(PACTR pactr, int32_t anid) const;
    bool FActorStudioSaveActionAs(PACTR pactr, int32_t anidSource, PCSZ pszName, int32_t *panidNew,
                                  bool fSingleFrame = fFalse);
    bool FActorStudioNewAction(PACTR pactr, int32_t anidSource, PCSZ pszName, int32_t *panidNew);
    bool FActorStudioSaveAction(PACTR pactr, int32_t anid);
    bool FActorStudioGetFrameSnapshot(PACTR pactr, int32_t anid, int32_t celn,
                                      ACTORSTUDIOFRAMESNAPSHOT *psnapshot) const;
    bool FActorStudioWriteFrameSnapshot(PACTR pactr, int32_t anid, int32_t celn,
                                        const ACTORSTUDIOFRAMESNAPSHOT *psnapshot, int32_t asfw);
    bool FActorStudioDeleteFrame(PACTR pactr, int32_t anid, int32_t celn);
    bool FActorStudioDuplicatePart(PACTR pactr, int32_t anidCurrent, int32_t celnCurrent,
                                   int32_t ipartSource, bool fKeepPositions,
                                   const BMAT34 *pbmat34Freeze, int32_t *pipartNew);
    bool FActorStudioDuplicatePartCore(PACTR pactr, int32_t anidCurrent, int32_t celnCurrent,
                                       int32_t ipartSource, bool fKeepPositions,
                                       const BMAT34 *pbmat34Freeze, int32_t *pipartNew,
                                       bool fAddUndo);
    bool FActorStudioDuplicatePartRange(PACTR pactr, int32_t anidCurrent, int32_t celnCurrent,
                                        int32_t ipartFirst, int32_t cpartSource, int32_t selectionKind,
                                        bool fKeepPositions, int32_t *pipartNewFirst);
    bool FActorStudioDuplicatePartRangeCore(PACTR pactr, int32_t anidCurrent, int32_t celnCurrent,
                                            int32_t ipartFirst, int32_t cpartSource, int32_t selectionKind,
                                            bool fKeepPositions, int32_t *pipartNewFirst, bool fAddUndo);
    bool FActorStudioSetPartMatrix(PACTR pactr, int32_t anid, int32_t celn, int32_t ipart,
                                   const BMAT34 *pbmat34, int32_t selectionKind = ivNil,
                                   int32_t ipartSelectionFirst = ivNil, int32_t cpartSelection = 0);
    bool FActorStudioSetPartMatrixCore(PACTR pactr, int32_t anid, int32_t celn, int32_t ipart,
                                       const BMAT34 *pbmat34, bool fAddUndo,
                                       int32_t selectionKind = ivNil,
                                       int32_t ipartSelectionFirst = ivNil, int32_t cpartSelection = 0);
    bool FActorStudioSetPartMatrices(PACTR pactr, int32_t anid, int32_t celn,
                                     const int32_t *prgipart, const BMAT34 *prgbmat34,
                                     int32_t cpart, int32_t selectionKind = ivNil,
                                     int32_t ipartSelectionFirst = ivNil, int32_t cpartSelection = 0);
    bool FActorStudioSetPartMatricesCore(PACTR pactr, int32_t anid, int32_t celn,
                                         const int32_t *prgipart, const BMAT34 *prgbmat34,
                                         int32_t cpart, bool fAddUndo,
                                         int32_t selectionKind = ivNil,
                                         int32_t ipartSelectionFirst = ivNil, int32_t cpartSelection = 0);
    bool FActorStudioDeletePartRange(PACTR pactr, int32_t ipartFirst, int32_t cpartDelete,
                                     int32_t selectionKind = kasskPart, int32_t anid = ivNil,
                                     int32_t celn = ivNil);
    bool FActorStudioDeletePartRangeCore(PACTR pactr, int32_t ipartFirst, int32_t cpartDelete,
                                         bool fClearUndo);
    bool FActorStudioCutPart(PACTR pactr, int32_t anid, int32_t celn, int32_t ipart,
                             int32_t selectionKind = kasskPart,
                             int32_t ipartSelectionFirst = ivNil, int32_t cpartSelection = 0);
    bool FActorStudioSpawnPart(PACTR pactr, int32_t anid, int32_t celn, int32_t ipart,
                               int32_t selectionKind = kasskPart,
                               int32_t ipartSelectionFirst = ivNil, int32_t cpartSelection = 0);
    bool FActorStudioCutParts(PACTR pactr, int32_t anid, int32_t celn,
                              const int32_t *prgipart, int32_t cpartPresence,
                              int32_t selectionKind, int32_t ipartSelectionFirst,
                              int32_t cpartSelection);
    bool FActorStudioSpawnParts(PACTR pactr, int32_t anid, int32_t celn,
                                const int32_t *prgipart, int32_t cpartPresence,
                                int32_t selectionKind, int32_t ipartSelectionFirst,
                                int32_t cpartSelection);
    bool FActorStudioChangePartPresence(PACTR pactr, int32_t anid, int32_t celn, int32_t ipart,
                                        bool fPresent, int32_t selectionKind,
                                        int32_t ipartSelectionFirst, int32_t cpartSelection);
    bool FActorStudioChangePartsPresence(PACTR pactr, int32_t anid, int32_t celn,
                                         const int32_t *prgipart, int32_t cpartPresence,
                                         bool fPresent, int32_t selectionKind,
                                         int32_t ipartSelectionFirst, int32_t cpartSelection);
    bool FActorStudioSetPartModelChidsCore(PACTR pactr, int32_t anid, int32_t celnFirst,
                                           int32_t ipart, const int16_t *prgchidModl,
                                           int32_t cchid);
    bool FActorStudioSetPartsModelChidsCore(PACTR pactr, int32_t anid, int32_t celnFirst,
                                            const int32_t *prgipart, int32_t cpartPresence,
                                            const int16_t *prgchidModl, int32_t cchid);
    static void SetPrecacheMode(bool fEnable);
    static bool FPrecacheMode(void);
    static void SetTestLightMode(bool fEnable);
    static bool FTestLightMode(void);
    static void SetShadowMode(bool fEnable);
    static bool FShadowMode(void);
    static bool FSceneDynamicLightingActive(void);
    static bool FSceneFlatLightingActive(void);
    static bool FSceneDefaultLightingShadersActive(void);
    static bool FSceneLightLabCombineLegacyActive(void);
    static void SetDiagnosticsMode(bool fEnable);
    static bool FDiagnosticsMode(void);
    static void DiagLog(PCSZ pszFormat, ...);
    // Lightweight editor-only lighting diagnostics. Unlike -logs, this keeps
    // one buffered file open and records only selection/frame/light/undo events.
    static void SetLightEditorLogMode(bool fEnable);
    static bool FLightEditorLogMode(void);
    static void LightEditorLog(PMVIE pmvie, PCSZ pszFormat, ...);
    static void SetMultiLogMode(bool fEnable);
    static bool FMultiLogMode(void);
    static void MultiLog(PMVIE pmvie, PCSZ pszFormat, ...);
    // Crash-persistent Actor Studio topology trace. Unlike multi.log this is
    // append-only across launches so a post-crash restart cannot erase the
    // final Duplicate stage before diagnostics are exported.
    static void ActorStudioDuplicateLog(PMVIE pmvie, PCSZ pszFormat, ...);
    static void DiagSetContext(PMVIE pmvie, PCSZ pszPhase);
    static void DiagCrash(void *pvExceptionPointers);
    static void SetPerformanceMode(bool fEnable);
    static bool FPerformanceMode(void);
    static uint64_t PerfNow(void);
    static uint32_t PerfElapsedUs(uint64_t qwStart);
    static void PerfAddSceneTimings(uint32_t cusecForceActors, uint32_t cusecForceTboxes,
                                    uint32_t cusecVisibility, uint32_t cusecCamera,
                                    uint32_t cusecPrerender);
    static void PerfAddBRenderTimings(uint32_t cusecBeginCallbacks, uint32_t cusecClean,
                                      uint32_t cusecSceneRender, uint32_t cusecPost,
                                      uint32_t cusecTotal, int32_t cWorldChildren);
    // actorlight20: low-overhead model-lifetime diagnostics. These feed
    // performance.csv only while -perf/-logs is active.
    static void PerfRecordSetActnCel(uint32_t cusec);
    static void PerfRecordActionFetch(uint32_t cusec);
    static void PerfRecordModelFetch(uint32_t cusecTotal, uint32_t cusecLookup, uint32_t cusecPbaco,
                                     CTG ctg, CNO cno);
    static void PerfRecordModelRead(uint32_t cusec, CTG ctg, CNO cno, bool fPreprepared);
    static void PerfRecordModelDestroy(uint32_t cusec);
    static void PerfRecordPartModel(uint32_t cusec, bool fSamePointer, bool fSameSource,
                                    bool fSameResource, bool fResourceReuse, bool fSameFile,
                                    bool fFileReuse, bool fSameContent, bool fContentReuse,
                                    uint32_t cusecContentCompare, bool fSameGeometry,
                                    bool fGeometryReuse, uint32_t cusecGeometryCompare,
                                    int32_t iGeometryReject);
    static void PerfRecordPartModelProvenance(bool fOldCrf, bool fNewCrf, bool fOldFingerprint,
                                              bool fNewFingerprint, bool fSizeMatch,
                                              bool fHashAMatch, bool fHashBMatch);
    static void PerfRecordPartMatrix(uint32_t cusec);
    static PMVIE PmvieNew(bool fHalfMode, PMCC pmcc, FNI *pfni = pvNil, CNO cno = cnoNil);
    // 3DMMv1.0: Create a movie and read it if
    // 3DMMv1.0:   pfni != pvNil
    static bool FReadRollCall(PCRF pcrf, CNO cno, PGST *ppgst, int32_t *paridLim = pvNil);
    // 3DMMv1.0: reads roll call for a given movie
    void ForceSaveAs(void)
    {
        ReleasePpo(&_pfilSave);
        _fFniSaveValid = fFalse;
    }
    void Flush(void);
    bool FReadOnly(void)
    {
        return FPure(_fReadOnly);
    }

    ~MVIE(void);

    //
    // 3DMMv1.0: MCC maintenance
    //
    PMCC Pmcc(void)
    {
        return _pmcc;
    } // 3DMMv1.0: Accessor for getting to client callbacks.
    void SetMcc(PMCC pmcc)
    {
        ReleasePpo(&_pmcc);
        _pmcc = pmcc;
        _pmcc->AddRef();
    }

    //
    // 3DMMv1.0: Title stuff
    //
    void GetName(PSTN pstnTitle) override; // 3DMMv1.0: Gets the title of the movie.
    PSTN PstnTitle(void)
    {
        return &_stnTitle;
    }
    void ResetTitle(void);

    //
    // 3DMMv1.0: Scene stuff
    //
    int32_t Cscen(void) // 3DMMv1.0: Returns number of scenes in movie
    {
        return _cscen;
    }
    int32_t Iscen(void) // 3DMMv1.0: Returns the current scene number
    {
        return _iscen;
    }
    bool FCameraTrackActive(void); // Current scene has one or more .3ct camera changes.
    bool FCameraTrackNeedsLiveActors(void); // Current track is changing at/around this frame.
    bool FDepthMotionTweenActive(void); // Current frame lies within a two-key-or-more Z tween.
    void RefreshTestLight(void); // Reinstall -l lights after a scene background/camera change.
    void UpdateTestLightAttachment(void); // Keep every Light Lab spotlight bound to its actor/prop.
    void ActorBodyReplaced(int32_t arid); // Rebind light/hidden state after actor undo/redo swaps BODY instances.
    bool FGetLightLabConfig(int32_t iscen, int32_t arid, LIGHTLAB *plight);
    bool FLightLabShadowSlotAvailable(int32_t iscen, int32_t aridIgnore) const;
    bool FActorHasLight(int32_t iscen, int32_t arid);
    bool FSceneLightsEnabled(int32_t iscen) const;
    bool FSceneHideLightObjects(int32_t iscen) const;
    bool FDefaultLightingShaders(void) const { return FPure(_fDefaultLightingShaders); }
    bool FSceneDefaultLightingShaders(int32_t iscen) const;
    bool FAnySceneDefaultLightingShadersDisagree(bool fUseShaders) const;
    bool FLightLabCombineLegacy(void) const { return FPure(_fLightLabCombineLegacy); }
    bool FSceneLightLabCombineLegacy(int32_t iscen) const;
    bool FFreeLookAlwaysLastKnown(void) const { return FPure(_fFreeLookAlwaysLastKnown); }
    bool FDontAskLightLabCut(void) const { return FPure(_fDontAskLightLabCut); }
    bool FDontAskShaderPropagation(void) const { return FPure(_fDontAskShaderPropagation); }
    bool FSetSceneLightsEnabled(int32_t iscen, bool fEnable, bool fUndo = fTrue);
    bool FSetSceneHideLightObjects(int32_t iscen, bool fEnable, bool fUndo = fTrue);
    bool FSetSceneDefaultLightingShaders(int32_t iscen, bool fEnable, bool fUndo = fTrue);
    bool FSetDefaultLightingShadersForMovie(bool fEnable, bool fDontAskAgain, bool fUndo = fTrue);
    bool FSetSceneLightLabCombineLegacy(int32_t iscen, bool fEnable, bool fUndo = fTrue);
    bool FSetLightLabCombineLegacyForMovie(bool fEnable, bool fUndo = fTrue);
    bool FSetFreeLookAlwaysLastKnown(bool fEnable, bool fUndo = fTrue);
    void SetDontAskLightLabCutCore(bool fEnable);
    bool FIsLightAttachmentHidden(int32_t arid) const;
    bool FObjectSelectable(int32_t arid) const;
    bool FSetObjectSelectable(int32_t arid, bool fSelectable, bool fUndo = fTrue);
    bool FToggleObjectSelectable(int32_t arid, bool fUndo = fTrue);
    bool FGetObjectProperties(int32_t iscen, int32_t arid, OBJECTPROPERTIES *pprop) const;
    bool FSetObjectProperties(const OBJECTPROPERTIES *pprop, bool fUndo = fTrue);
    bool FOpenObjectPropertiesEditor(int32_t arid);
    void UpdateObjectShadowProperties(void);
    bool FDefaultMusicForNewScenes(void) const { return FPure(_fDefaultMusicForNewScenes); }
    void SetDefaultMusicForNewScenes(bool fEnable);
    bool FExperimental100xGrow(void) const { return FPure(_fExperimental100xGrow); }
    void SetExperimental100xGrow(bool fEnable);
    bool FExperimental100xShrink(void) const { return FPure(_fExperimental100xShrink); }
    void SetExperimental100xShrink(bool fEnable);
    float CameraMoveSpeed(void) const { return _flCameraMoveSpeed; }
    void SetCameraMoveSpeed(float flSpeed);
    float CameraMouseSensitivity(void) const { return _flCameraMouseSensitivity; }
    void SetCameraMouseSensitivity(float flSensitivity);
    bool FSetLightLabConfig(const LIGHTLAB *plight);
    bool FOpenLightLabEditor(void);
    bool FRemoveLightLabConfigCore(int32_t iscen, int32_t arid);
    bool FRestoreLightLabConfigCore(const LIGHTLAB *plight);
    bool FManualCameraMode(void)
    {
        return FPure(_fManualCameraMode);
    }
    bool FManualCameraRecording(void)
    {
        return FPure(_fManualCameraRecording);
    }
    bool FFreeLookMode(void)
    {
        return FPure(_fFreeLookMode);
    }
    bool FFreeLookOverride(void)
    {
        return FPure(_fFreeLookOverride);
    }
    bool FCameraInputActive(void)
    {
        return FPure(_fManualCameraMode || _fFreeLookMode);
    }
    bool FSetManualCameraMode(bool fEnable);
    bool FStartManualCameraRecording(void);
    void RequestStopManualCameraRecording(void);
    bool FStartFreeLook(bool fOppositeStartPolicy = fFalse);
    void ResetFreeLook(void);
    void EndFreeLook(bool fRevert);
    bool FMoveManualCamera(float dForward, float dStrafe, bool fFly, bool fUpdateView = fTrue);
    bool FLookManualCamera(float dYaw, float dPitch, bool fUpdateView = fTrue);
    bool FRollManualCamera(float dRoll, bool fUpdateView = fTrue);
    bool FCommitManualCameraEdit(bool fExitMode);
    void CancelManualCameraEdit(void);
    bool FStepManualCamera(int32_t dnfrm);
    bool FAppendManualCameraFrame(void);
    bool FInsertFramesRelative(bool fBefore, bool fBlank, int32_t cfrm = 1);
    bool FInsertCameraTrackFramesAfter(int32_t iscen, int32_t nfrm, int32_t cfrm);
    bool FInsertCameraTrackFramesBefore(int32_t iscen, int32_t nfrm, int32_t cfrm);
    bool FPrependCameraTrackFrames(int32_t iscen, int32_t cfrm);
    bool FPrepareNativeFrameInsert(bool fBefore);
    bool FCompleteNativeFrameInsert(bool fBefore);
    bool FAddNativeEndFrames(int32_t cfrm = 1, bool fBlank = fFalse);
    bool FAddNativeStartBlankFrames(int32_t cfrm = 1);
    bool FOpenDepthMotionTweenEditor(void);
    void GetDepthMotionTweenText(int32_t iscen, int32_t itween,
                                 const CTTWEEN *pctweenDefault, PSTN pstnText);
    bool FSetDepthMotionTweenText(int32_t iscen, int32_t itween, const achar *pszText);
    bool FDeleteDepthMotionTween(int32_t iscen, int32_t itween);
    bool FManualCameraFrameActive(void);
    bool FOpenManualCameraFrameEditor(void);
    void GetManualCameraFrameText(int32_t iscen, int32_t nfrm, PSTN pstnText);
    bool FSetManualCameraFrameText(int32_t iscen, int32_t nfrm, const achar *pszText);
    bool FDeleteManualCameraFrame(int32_t iscen, int32_t nfrm);
    bool FDeleteManualCameraFrameCurrent(void);
    bool FAddCameraTrackUndo(const achar *pszUndoName);
    bool FGetCameraTrackState(CTSTATE *pctstate);
    void SwapCameraTrackState(CTSTATE *pctstate);
    void TrimCameraTrackAfter(int32_t iscen, int32_t nfrmCut);
    void TrimCameraTrackBefore(int32_t iscen, int32_t nfrmCut);
    void ApplyCameraTrack(void);  // Apply the current scene/frame camera offset before drawing.
    void SetBrowserCameraFollow(int32_t arid, bool fEnable);
    bool FBrowserCameraFollowActive(void)
    {
        return FPure(_fBrowserCameraFollow);
    }
    bool FUpdateBrowserCameraFollow(void);
    bool FLiveCameraDisplacedFromFrame(void);
    void AdjustCameraTrackInsertionPoint(BRS *pxr, BRS *pyr, BRS *pzr);
    BRA BraCameraTrackYaw(void);
    bool FAlignCameraTrackYawToSelectedActor(void);
    bool FSwitchScen(int32_t iscen);                             // 3DMMv1.0: Loads and returns pointer to scene iscen,
                                                                 // 3DMMv1.0:   saving any current scene.
    bool FRemScen(int32_t iscen);                                // 3DMMv1.0: Removes a scene from the movie, and undo
    bool FChangeCam(int32_t camid);                              // 3DMMv1.0: Change the camera view in the scene.
    bool FInsTbox(RC *prc, bool fStory);                         // 3DMMv1.0: Insert a text box into the scene.
    bool FHideTbox(void);                                        // 3DMMv1.0: Hide selected text box from the scene at this fram.
    bool FNukeTbox(void);                                        // 3DMMv1.0: Remove selected text box from the scene.
    void SelectTbox(int32_t itbox);                              // 3DMMv1.0: Select the itbox'th text box in the frame.
    void SetPaintAcr(ACR acr);                                   // 3DMMv1.0: Sets color that painting will occur with.
    void SetOnnTextCur(int32_t onn);                             // 3DMMv1.0: Sets font that text will be in
    void SetDypFontTextCur(int32_t dypFont);                     // 3DMMv1.0: Sets font size that text will be in
    void SetStyleTextCur(uint32_t grfont);                       // 3DMMv1.0: Sets font style that text will be in
    bool FInsActr(PTAG ptag);                                    // 3DMMv1.0: Insert an actor into the scene.
    bool FRemActr(void);                                         // 3DMMv1.0: Remove selected actor from scene.
    bool FAddOnstage(int32_t arid);                              // 3DMMv1.0: Bring this actor onto the stage.
    bool FRotateActr(BRA xa, BRA ya, BRA za, bool fFromHereFwd); // 3DMMv1.0: Rotate selected actor by degrees
    bool FSquashStretchActr(BRS brs);                            // 3DMMv1.0: Squash/Stretch selected actor
    bool FSoonerLaterActr(int32_t nfrm);                         // 3DMMv1.0: Sooner/Later selected actor
    bool FScaleActr(BRS brs, bool fExtended = fFalse);             // 3DMMv1.0: Scale selected actor
    bool FCostumeActr(int32_t ibprt, PTAG ptag, int32_t cmid, tribool fCustom);
    bool FAddScen(PTAG ptag);          // 3DMMv1.0: Add a scene after the current one, or change
                                       // 3DMMv1.0:   change bkgd if scene is empty
    bool FSetTransition(TRANS trans);  // 3DMMv1.0: Set the transition type for the current scene.
    void Play(void);                   // 3DMMv1.0: Start/Stop a movie playing.
    bool FPause(WIT wit, int32_t dts); // 3DMMv1.0: Insert a pause here.

    bool FAddToCmvi(PCMVI pcmvi, int32_t *piscendIns);
    // 3DMMv1.0: Add this movie to the CMVI
    bool FSetCmvi(PCMVI pcmvi); // 3DMMv1.0: Re-build the movie from the CMVI
    bool _FAddMvieToRollCall(CNO cno, int32_t aridMin);
    // 3DMMv1.0: Updates roll call for an imported movie
    bool _FInsertScend(PGL pglscend, int32_t iscend, PSCEND pscend);
    // 3DMMv1.0: Insert an imported scene
    void _DeleteScend(PGL pglscend, int32_t iscend); // 3DMMv1.0: Delete an imported scene
    bool _FAdoptMsndInMvie(PCFL pcfl, CNO cnoScen);  // 3DMMv1.0: Adopt msnd chunks as children of the movie

    bool FAddBkgdSnd(PTAG ptag, tribool fLoop, tribool fQueue, int32_t vlm = vlmNil,
                     int32_t sty = styNil); // 3DMMv1.0: Adds a sound
    bool FAddActrSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fActnCel, int32_t vlm,
                     int32_t sty); // 3DMMv1.0: Adds a sound

    //
    // 3DMMv1.0: Auto save stuff
    //
    bool FAutoSave(PFNI pfni = pvNil, bool fCleanRollCall = fFalse); // 3DMMv1.0: Save movie in temp file
    bool FSaveTagSnd(TAG *ptag)
    {
        return TAGM::FSaveTag(ptag, _pcrfAutoSave, fTrue);
    }
    bool FCopySndFileToMvie(PFIL pfil, int32_t sty, CNO *pcno, PSTN pstn = pvNil);
    bool FVerifyVersion(PCFL pcfl, CNO *pcno = pvNil);
    bool FEnsureAutosave(PCRF *pcrf = pvNil);
    bool FCopyMsndFromPcfl(PCFL pcfl, CNO cnoSrc, CNO *pcnoDest);
    bool FResolveSndTag(PTAG ptag, CHID chid, CNO cnoScen = cnoNil, PCRF pcrf = pvNil);
    bool FChidFromUserSndCno(CNO cno, CHID *pchid);
    void SetDocClosing(bool fClose)
    {
        _fDocClosing = fClose;
    }
    bool FQueryDocClosing(void)
    {
        return _fDocClosing;
    }
    bool FQueryGCSndsOnClose(void)
    {
        return _fGCSndsOnClose;
    }
    bool FUnusedSndsUser(bool *pfHaveValid = pvNil);

    //
    // 3DMMv1.0: Roll call
    //
    bool FGetArid(int32_t iarid, int32_t *parid, PSTN pstn, int32_t *pcactRef,
                  PTAG ptagTmpl = pvNil); // 3DMMv1.0: return actors one by one
    bool FChooseArid(int32_t arid);       // 3DMMv1.0: user chose arid in roll call
    int32_t AridSelected(void);
    bool FGetName(int32_t arid, PSTN pstn);      // Return the editable/stored name of a specific actor.
    bool FGetDisplayName(int32_t arid, PSTN pstn); // Return editor-list name, adding synthetic badges such as (L).
    bool FNameActr(int32_t arid, PSTN pstn);     // 3DMMv1.0: Set the name of this actor.
    void ChangeActrTag(int32_t arid, PTAG ptag); // 3DMMv1.0: Change an actor's TMPL tag
    int32_t CmactrMac(void)
    {
        AssertThis(0);
        return _pgstmactr->IvMac();
    }
    bool FIsPropBrwsIarid(int32_t iarid); // 3DMMv1.0: Identify the roll call browser iarids
    bool FIsIaridTdt(int32_t iarid);      // 3DMMv1.0: 3d spletter
    bool FIsVxpBrwsIarid(int32_t iarid);  // 4DMM: native VXP import exposed in Add Actor/Prop browser

    //
    // 3DMMv1.0: Overridden DOCB functions
    //
    bool FGetFni(FNI *pfni) override;                  // 3DMMv1.0: For saving to a file
    bool FSave(int32_t cid) override;                  // 3DMMv1.0: For saving to a file, (calls FGetFni and FSaveToFni)
    bool FSaveToFni(FNI *pfni, bool fSetFni) override; // 3DMMv1.0: For doing a Save As or Save
    PDMD PdmdNew(void) override;                       // 3DMMv1.0: Do not use!
    bool FGetFniSave(FNI *pfni) override;              // 3DMMv1.0: For saving via the portfolio.

    //
    // 3DMMv1.0: Drawing stuff
    //
    void InvalViews(void);       // 3DMMv1.0: Invalidates all views on the movie.
    void InvalViewsAndScb(void); // 3DMMv1.0: Invalidates all views of movie and scroll bars.
    void MarkViews(void);        // 3DMMv1.0: Marks all views on the movie.
    TRANS Trans(void)
    {
        return _trans;
    } // 3DMMv1.0: Current transition in effect.
    void SetTrans(TRANS trans)
    {
        _trans = trans;
    }                                                                 // 3DMMv1.0: Set transition
    void DoTrans(PGNV pgnvDst, PGNV pgnvSrc, RC *prcDst, RC *prcSrc); // 3DMMv1.0: Draw current transition into GNVs

    //
    // 3DMMv1.0: Sound stuff
    //
    bool FSoundsEnabled(void)
    {
        return (_fSoundsEnabled);
    }
    void SetFSoundsEnabled(bool f)
    {
        _fSoundsEnabled = f;
    }

    //
    // 3DMMv1.0: End client callable functions
    //

    //
    // 3DMMv1.0: Begin internal movie engine functions
    //

    //
    // 3DMMv1.0: Command handlers
    //
    bool FCmdAlarm(CMD *pcmd);  // 3DMMv1.0: Called at timer expiration (playback).
    bool FCmdRender(CMD *pcmd); // 3DMMv1.0: Called to render a frame during playback.

    //
    // 3DMMv1.0: Automated test APIs
    //
    int32_t LwQueryExists(int32_t lwType, int32_t lwId);     // 3DMMv1.0: Called by other apps to find if an actor/tbox exists.
    int32_t LwQueryLocation(int32_t lwType, int32_t lwId);   // 3DMMv1.0: Called by other apps to find where actor/tbox exists.
    int32_t LwSetMoviePos(int32_t lwScene, int32_t lwFrame); // 3DMMv1.0: Called by other apps to set the movie position.

    //
    // 3DMMv1.0: Scene stuff
    //
    PSCEN Pscen(void)
    {
        return _pscenOpen;
    }                                              // 3DMMv1.0: The currently open scene.
    bool FInsScenCore(int32_t iscen, SCEN *pscen); // 3DMMv1.0: Insert this scene as scene number.
    bool FNewScenInsCore(int32_t iscen);           // 3DMMv1.0: Inserts a blank scene before iscen
    bool FRemScenCore(int32_t iscen);              // 3DMMv1.0: Removes a scene from the movie
    bool FPasteActr(PACTR pactr, bool fInPlace = fFalse); // 3DMMv1.0: Pastes an actor into current scene.
    bool FPasteActrPath(PACTR pactr);              // 3DMMv1.0: Pastes the path onto selected actor.
    PMSQ Pmsq(void)
    {
        return _pmsq;
    } // 3DMMv1.0: Sound queue

    //
    // 3DMMv1.0: Runtime Pausing
    //
    void DoPause(WIT wit, int32_t dts) // 3DMMv1.0: Make the movie pause during run.
    {
        _wit = wit;
        _dts = dts;
    }

    //
    // 3DMMv1.0: Material stuff
    //
    bool FInsertMtrl(PMTRL pmtrl, PTAG ptag); // 3DMMv1.0: Inserts a material into this movie.

    //
    // 3DMMv1.0: 3-D Text stuff
    //
    bool FInsTdt(PSTN pstn, int32_t tdts, PTAG ptagTdf); // 3DMMv1.0: Inserts a TDT into this movie.
    bool FChangeActrTdt(PACTR pactr, PSTN pstn, int32_t tdts, PTAG ptagTdf);

    //
    // 3DMMv1.0: Marking (overridden DOCB methods)
    //
    virtual bool FDirty(void) override // 3DMMv1.0: Has the movie changed since last saved?
    {
        return _fAutosaveDirty || _fDirty;
    }
    virtual void SetDirty(bool fDirty = fTrue) override // 3DMMv1.0: Mark the movie as changed.
    {
        _fAutosaveDirty = fDirty;
    }

    //
    // 3DMMv1.0: Roll call
    //
    bool FAddToRollCall(ACTR *pactr, PSTN pstn);                    // 3DMMv1.0: Add an actor to the roll call
    bool FImport4DMMVxpContent(PFNI pfniContent, bool fProp, int32_t *pcImported = pvNil,
                                 bool fVxp2 = fFalse, PCSZ pszVxp2TextureBase = pvNil);
    void RemFromRollCall(ACTR *pactr, bool fDelIfOnlyRef = fFalse); // 3DMMv1.0: Remove an actor from the roll call.
    bool F4DMMActorStudioExposeCurrentPlacementInRollCall(void);
    bool F4DMMActorStudioConsumePreaddedPlacement(PACTR pactr);
    void BuildActionMenu(void);                                     // 3DMMv1.0: Called when the selected actor has changed.

    //
    // 3DMMv1.0: Overridden DOCB functions
    //
    PDDG PddgNew(PGCB pgcb) override;   // 3DMMv1.0: For creating a view on a movie.
    virtual bool FAddUndo(PMUNB pmunb); // 3DMMv1.0: Add an item to the undo list
    void ClearUndo(void) override;

    //
    // 3DMMv1.0: Accessors for MVUs only.
    //
#ifdef DEBUG
    void SetFPlaying(bool f)
    {
        _fPlaying = f;
        if (!f)
            SetFWriteBmps(fFalse);
    }  // 3DMMv1.0: Set the playing flag.
#else  // 3DMMv1.0: DEBUG
    void SetFPlaying(bool f)
    {
        _fPlaying = f;
    } // 3DMMv1.0: Set the playing flag.
#endif // 3DMMv1.0: !DEBUG
    void SetFStopPlaying(bool f)
    {
        _fStopPlaying = f;
    } // 3DMMv1.0: INTERNAL USE ONLY
    bool FStopPlaying(void)
    {
        return (_fStopPlaying);
    } // 3DMMv1.0: INTERNAL USE ONLY
    bool FPlaying(void)
    {
        return _fPlaying;
    } // 3DMMv1.0: Query the playing flag.
    PCLOK Pclok()
    {
        return &_clok;
    } // 3DMMv1.0: For getting the clock for playing
    void SetFIdleSeen(bool fIdle)
    {
        _fIdleSeen = fIdle;
    }
    bool FIdleSeen(void)
    {
        return _fIdleSeen;
    }

    //
    // 3DMMv1.0: Accessor for getting to the Brender world.
    //
    PBWLD Pbwld(void)
    {
        return _pbwld;
    }

    //
    // 3DMMv1.0: Frame rate information
    //
    int32_t Cnfrm(void)
    {
        return _cnfrm;
    }
    uint32_t TsStart(void)
    {
        return _tsStart;
    }

    //
    // 3DMMv1.0: Thumbnail stuff
    //
    PGL PglclrThumbPalette(void)
    {
        AssertThis(0);
        return _pglclrThumbPalette;
    }
    void SetThumbPalette(PGL pglclr)
    {
        ReleasePpo(&_pglclrThumbPalette);
        _pglclrThumbPalette = pglclr;
        pglclr->AddRef();
    }
};

#endif // 3DMMv1.0: !MOVIE_H
