/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    THIS IS A CODE REVIEWED FILE

    Basic scene classes:
        Scene (SCEN)

            BASE ---> SCEN

        Scene Actor Undo Object (SUNA)

            BASE ---> UNDB ---> MUNB ---> SUNA

***************************************************************************/

#ifndef SCEN_H
#define SCEN_H

//
// 3DMMv1.0: Undo object for actor operations
//
typedef class SUNA *PSUNA;

#define SUNA_PAR MUNB

// 3DMMv1.0: Undo types
enum
{
    utAdd = 0x1,
    utDel,
    utRep,
};

#define kclsSUNA KLCONST4('S', 'U', 'N', 'A')
class SUNA : public SUNA_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    PACTR _pactr;
    int32_t _ut; // 3DMMv1.0: Tells which type of undo this is.

    // 4DMM: Add-Actor undo/redo can carry an attached Light Lab record so a
    // copied light follows the copied object through undo and redo as one edit.
    bool _fHasLightLab;
    bool _fLightEnabled;
    bool _fLightGenerateShadows;
    bool _fLightAttachmentHideable;
    int32_t _lightIntensity;
    float _lightEdgeGradient;
    float _lightDiameter;
    float _lightRange;
    achar _szLightShape[16];

    // v238: Add-Actor undo/redo also carries copied Object Properties so
    // clipboard property inheritance survives undo and redo as one edit.
    bool _fHasObjectProperties;
    bool _fObjectFlushOverlap;
    bool _fObjectCastShadows;

    SUNA(void)
    {
        _pactr = pvNil;
        _ut = 0;
        _fHasLightLab = fFalse;
        _fLightEnabled = fFalse;
        _fLightGenerateShadows = fFalse;
        _fLightAttachmentHideable = fFalse;
        _lightIntensity = 0;
        _lightEdgeGradient = 0.0f;
        _lightDiameter = 0.0f;
        _lightRange = 500.0f;
        ClearPb(_szLightShape, SIZEOF(_szLightShape));
        _fHasObjectProperties = fFalse;
        _fObjectFlushOverlap = fFalse;
        _fObjectCastShadows = fTrue;
    }

  public:
    static PSUNA PsunaNew(void);
    ~SUNA(void);

    void SetActr(PACTR pactr)
    {
        _pactr = pactr;
    }
    void SetType(int32_t ut)
    {
        _ut = ut;
    }
    void SetLightLab(bool fEnabled, bool fGenerateShadows, bool fAttachmentHideable, int32_t intensity,
                     float edgeGradient, float diameter, float range, const achar *pszShape)
    {
        _fHasLightLab = fTrue;
        _fLightEnabled = fEnabled;
        _fLightGenerateShadows = fGenerateShadows;
        _fLightAttachmentHideable = fAttachmentHideable;
        _lightIntensity = intensity;
        _lightEdgeGradient = edgeGradient;
        _lightDiameter = diameter;
        _lightRange = range;
        int32_t ich = 0;
        if (pszShape != pvNil)
        {
            for (; ich < (int32_t)SIZEOF(_szLightShape) - 1 && pszShape[ich] != chNil; ich++)
                _szLightShape[ich] = pszShape[ich];
        }
        _szLightShape[ich] = chNil;
    }

    void SetObjectProperties(bool fFlushOverlap, bool fCastShadows)
    {
        _fHasObjectProperties = fTrue;
        _fObjectFlushOverlap = FPure(fFlushOverlap);
        _fObjectCastShadows = FPure(fCastShadows);
    }

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

//
// 3DMMv1.0: Different reasons for pausing in a scene
//
enum WIT
{
    witNil,
    witUntilClick,
    witUntilSnd,
    witForTime,
    witLim
};

//
// 3DMMv1.0: Functionality that can be turned off and on.  If the bit
// 3DMMv1.0: is set, it is disabled.
//
enum
{
    fscenSounds = 0x1,
    fscenPauses = 0x2,
    fscenTboxes = 0x4,
    fscenActrs = 0x8,
    fscenPosition = 0x10,
    fscenAction = 0x20,
    fscenCams = 0x40,
    fscenAll = 0xFFFF
};

typedef struct SSE *PSSE;
typedef struct TAGC *PTAGC;

typedef class SCEN *PSCEN;

//
// 3DMMv1.0: Notes:
//
// 3DMMv1.0:	This assumes that struct SND contains at least,
// 3DMMv1.0:		- Everything necessary to play the sound.
//
// 3DMMv1.0:	This assumes that struct TBOX contains at least,
// 3DMMv1.0:		- Everything necessary to display the text.
// 3DMMv1.0:		- Enumerating through text boxes in a scene is not necessary.
//

#define SCEN_PAR BASE
#define kclsSCEN KLCONST4('S', 'C', 'E', 'N')
class SCEN : public SCEN_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    typedef struct SEV *PSEV;

    //
    // 3DMMv1.0: These variables keep track of the internal frame numbers.
    //
    int32_t _nfrmCur;   // 3DMMv1.0: Current frame number
    int32_t _nfrmLast;  // 3DMMv1.0: Last frame number in scene.
    int32_t _nfrmFirst; // 3DMMv1.0: First frame number in scene.

    //
    // 3DMMv1.0: Frames with events in them.  This stuff works as follows.
    // 3DMMv1.0:   _isevFrmLim is the index into the GG of a sev with nfrm > nCurFrm.
    //
    PGG _pggsevFrm;      // 3DMMv1.0: List of events that occur in frames.
    int32_t _isevFrmLim; // 3DMMv1.0: Next event to process.

    //
    // 3DMMv1.0: Global information
    //
    STN _stnName;         // 3DMMv1.0: Name of this scene
    PGL _pglpactr;        // 3DMMv1.0: List of actors in the scene.
    PGL _pglptbox;        // 3DMMv1.0: List of text boxes in the scene.
    PGG _pggsevStart;     // 3DMMv1.0: List of frame independent events.
    PMVIE _pmvie;         // 3DMMv1.0: Movie this scene is a part of.
    PBKGD _pbkgd;         // 3DMMv1.0: Background for this scene.
    uint32_t _grfscen;    // 3DMMv1.0: Disabled functionality.
    PACTR _pactrSelected;  // Primary selected actor, if any
    PACTR _pactrSelected2; // Compatibility mirror of the second selected actor, if any
    PGL _pglaridSelected;  // Authoritative -multi selection list (int32_t ARIDs)
    PTBOX _ptboxSelected;  // 3DMMv1.0: Currently selected tbox, if any
    TRANS _trans;         // 3DMMv1.0: Transition at the end of the scene.
    PMBMP _pmbmp;         // 3DMMv1.0: The thumbnail for this scene.
    PSSE _psseBkgd;       // 3DMMv1.0: Background scene sound (starts playing
                          // 3DMMv1.0: at start time even if snd event is
                          // 3DMMv1.0: earlier)
    int32_t _nfrmSseBkgd; // 3DMMv1.0: Frame at which _psseBkgd starts
    TAG _tagBkgd;         // 3DMMv1.0: Tag to current BKGD

  protected:
    SCEN(PMVIE pmvie);
    ~SCEN(void);

    //
    // 3DMMv1.0: Event stuff
    //
    bool _FPlaySev(PSEV psev, void *qvVar, uint32_t grfscen); // 3DMMv1.0: Plays a single scene event.
    bool _FUnPlaySev(PSEV psev, void *qvVar);                 // 3DMMv1.0: Undoes a single scene event.
    bool _FAddSev(PSEV psev, int32_t cbVar, void *pvVar);     // 3DMMv1.0: Adds scene event to the current frame.
    void _MoveBackFirstFrame(int32_t nfrm);

    //
    // 3DMMv1.0: Dirtying stuff
    //
    void _MarkMovieDirty(void);
    void _DoPrerenderingWork(bool fStartNow); // 3DMMv1.0: Does any prerendering for _nfrmCur
    void _EndPrerendering(void);              // 3DMMv1.0: Stops prerendering

    //
    // 3DMMv1.0: Make actors go to a specific frame
    //
    bool _FForceActorsToFrm(int32_t nfrm, bool *pfSoundInFrame = pvNil);
    bool _FForceTboxesToFrm(int32_t nfrm);

    //
    // 3DMMv1.0: Thumbnail routines
    //
    void _UpdateThumbnail(void);

  public:
    //
    // 3DMMv1.0: Create and destroy
    //
    static SCEN *PscenNew(PMVIE pmvie);                      // 3DMMv1.0: Returns pvNil if it fails.
    static SCEN *PscenRead(PMVIE pmvie, PCRF pcrf, CNO cno); // 3DMMv1.0: Returns pvNil if it fails.
    bool FWrite(PCRF pcrf, CNO *pcno);                       // 3DMMv1.0: Returns fFalse if it fails, else the cno written.
    static void Close(PSCEN *ppscen);                        // 3DMMv1.0: Public destructor
    void RemActrsFromRollCall(bool fDelIfOnlyRef = fFalse);  // 3DMMv1.0: Removes actors from movie roll call.
    bool FAddActrsToRollCall(void);                          // 3DMMv1.0: Adds actors from movie roll call.

    //
    // 3DMMv1.0: Tag collection
    //
    static bool FAddTagsToTagl(PCFL pcfl, CNO cno, PTAGL ptagl);

    //
    // 3DMMv1.0: Frame functions
    //
    bool FPlayStartEvents(bool fActorsOnly = fFalse); // 3DMMv1.0: Play all one-time starting scene events.
    void InvalFrmRange(void);                         // 3DMMv1.0: Mark the frame count dirty
    bool FGotoFrm(int32_t nfrm);                      // 3DMMv1.0: Jumps to an arbitrary frame.
    int32_t Nfrm(void)                                // 3DMMv1.0: Returns the current frame number
    {
        return (_nfrmCur);
    }
    void PreserveFirstFrameBoundary(int32_t nfrmFirst); // Restore an earlier native scene boundary after object lifetime edits.
    int32_t NfrmFirst(void) // 3DMMv1.0: Returns the number of the first frame in the scene.
    {
        return (_nfrmFirst);
    }
    int32_t NfrmLast(void) // 3DMMv1.0: Returns the number of the last frame in the scene.
    {
        return (_nfrmLast);
    }
    bool FReplayFrm(uint32_t grfscen); // 3DMMv1.0: Replay events in this scene.

    //
    // 3DMMv1.0: Undo accessor functions
    //
    void SetNfrmCur(int32_t nfrm)
    {
        _nfrmCur = nfrm;
    }

    //
    // 3DMMv1.0: Edit functions
    //
    void SetMvie(PMVIE pmvie); // 3DMMv1.0: Sets the associated movie.
    void GetName(PSTN pstn)    // 3DMMv1.0: Gets name of current scene.
    {
        *pstn = _stnName;
    }
    void SetNameCore(PSTN pstn) // 3DMMv1.0: Sets name of current scene.
    {
        _stnName = *pstn;
    }
    bool FSetName(PSTN pstn); // 3DMMv1.0: Sets name of current scene, and undo
    bool FChopCore(void);     // 3DMMv1.0: Chops off the rest of the scene.
    bool FChop(void);         // 3DMMv1.0: Chops off the rest of the scene and undo
    bool FChopBackCore(void); // 3DMMv1.0: Chops off the rest of the scene, backwards.
    bool FChopBack(void);     // 3DMMv1.0: Chops off the rest of the scene, backwards, and undo.
    bool FAddSnapshotUndo(const achar *pszUndoName); // Full scene + camera-track snapshot.
    bool FAddNativeFrameUndo(bool fBefore, bool fBlank, int32_t cfrm);
    bool FFrameBlank(int32_t nfrm) const;
    bool FNativeAppendFramesCore(int32_t cfrm, bool fBlank);
    bool FNativePrependBlankFramesCore(int32_t cfrm);
    bool FNativeRemoveEndFramesCore(int32_t cfrm);
    bool FNativeRemoveStartFramesCore(int32_t cfrm);
    bool FInsertFramesAtCurrent(int32_t cfrm, bool fBefore, bool fBlank,
                                bool fAddUndo = fTrue);

    //
    // 3DMMv1.0: Transition functions
    //
    void SetTransitionCore(TRANS trans) // 3DMMv1.0: Set the final transition to be.
    {
        _trans = trans;
    }
    bool FSetTransition(TRANS trans); // 3DMMv1.0: Set the final transition to be and undo.
    TRANS Trans(void)
    {
        return _trans;
    } // 3DMMv1.0: Returns the transition setting.
    // 3DMMv1.0: These two operate a specific SCEN chunk rather than a SCEN in memory
    static bool FTransOnFile(PCRF pcrf, CNO cno, TRANS *ptrans);
    static bool FSetTransOnFile(PCRF pcrf, CNO cno, TRANS trans);

    //
    // 3DMMv1.0: State functions
    //
    void Disable(uint32_t grfscen) // 3DMMv1.0: Disables functionality.
    {
        _grfscen |= grfscen;
    }
    void Enable(uint32_t grfscen) // 3DMMv1.0: Enables functionality.
    {
        _grfscen &= ~grfscen;
    }
    int32_t GrfScen(void) // 3DMMv1.0: Currently disabled functionality.
    {
        return _grfscen;
    }
    bool FIsEmpty(void); // 3DMMv1.0: Is the scene empty?

    //
    // 3DMMv1.0: Actor functions
    //
    bool FAddActrCore(ACTR *pactr); // 3DMMv1.0: Adds an actor to the scene at current frame.
    bool FAddActr(ACTR *pactr);     // 3DMMv1.0: Adds an actor to the scene at current frame, and undo
    void RemActrCore(int32_t arid); // 3DMMv1.0: Removes an actor from the scene.
    bool FRemActr(int32_t arid);    // 3DMMv1.0: Removes an actor from the scene, and undo
    PACTR PactrSelected(void)       // 3DMMv1.0: Returns selected actor
    {
        return _pactrSelected;
    }
    void SelectActr(ACTR *pactr);                               // 3DMMv1.0: Sets the selected actor
    void SelectActrAdd(ACTR *pactr);                            // Shift-adds another actor/prop/3D word selection
    void SelectActrRemove(ACTR *pactr);                         // Ctrl-click removes one object from -multi selection
    int32_t CactrSelected(void);                                // Number of selected scene objects
    PACTR PactrSelectedAt(int32_t iactr);                       // Selected object by selection index
    bool FActrSelected(int32_t arid);                           // Is this ARID currently selected?
    PACTR PactrSelected2(void)                                  // Compatibility mirror of second selection
    {
        return _pactrSelected2;
    }
    PACTR PactrFromPt(int32_t xp, int32_t yp, int32_t *pibset); // 3DMMv1.0: Gets actor pointed at by the mouse.
    PGL PglRollCall(void)                                       // 3DMMv1.0: Return a list of all actors in scene.
    {
        return (_pglpactr);
    } // 3DMMv1.0: Only to be used by the movie-class
    void HideActors(void);
    void ShowActors(void);
    PACTR PactrFromArid(int32_t arid); // 3DMMv1.0: Finds a current actor in this scene.
    int32_t Cactr(void)
    {
        return (_pglpactr == pvNil ? 0 : _pglpactr->IvMac());
    }

    //
    // 3DMMv1.0: Sound functions
    //
    bool FAddSndCore(bool fLoop, bool fQueue, int32_t vlm, int32_t sty, int32_t ctag,
                     PTAG prgtag); // 3DMMv1.0: Adds a sound to the current frame.
    bool FAddSndCoreTagc(bool fLoop, bool fQueue, int32_t vlm, int32_t sty, int32_t ctagc, PTAGC prgtagc);
    bool FAddSnd(PTAG ptag, bool fLoop, bool fQueue, int32_t vlm,
                 int32_t sty);                             // 3DMMv1.0: Adds a sound to the current frame, and undo
    void RemSndCore(int32_t sty);                          // 3DMMv1.0: Removes the sound from current frame.
    bool FRemSnd(int32_t sty);                             // 3DMMv1.0: Removes the sound from current frame, and undo
    bool FGetSnd(int32_t sty, bool *pfFound, PSSE *ppsse); // 3DMMv1.0: Allows for retrieval of sounds.
    void PlayBkgdSnd(void);
    bool FQuerySnd(int32_t sty, PGL *pgltagSnd, int32_t *pvlm, bool *pfLoop);
    void SetSndVlmCore(int32_t sty, int32_t vlmNew);
    void UpdateSndFrame(void);
    bool FResolveAllSndTags(CNO cnoScen);

    //
    // 3DMMv1.0: Text box functions
    //
    bool FAddTboxCore(PTBOX ptbox);      // 3DMMv1.0: Adds a text box to the current frame.
    bool FAddTbox(PTBOX ptbox);          // 3DMMv1.0: Adds a text box to the current frame.
    bool FRemTboxCore(PTBOX ptbox);      // 3DMMv1.0: Removes a text box from the scene.
    bool FRemTbox(PTBOX ptbox);          // 3DMMv1.0: Removes a text box from the scene.
    PTBOX PtboxFromItbox(int32_t itbox); // 3DMMv1.0: Returns the ith tbox in this frame.
    PTBOX PtboxSelected(void)            // 3DMMv1.0: Returns the tbox currently selected.
    {
        return _ptboxSelected;
    }
    void SelectTbox(PTBOX ptbox); // 3DMMv1.0: Selects this tbox.
    void HideTboxes(void);        // 3DMMv1.0: Hides all text boxes.
    int32_t Ctbox(void)
    {
        return (_pglptbox == pvNil ? 0 : _pglptbox->IvMac());
    }

    //
    // 3DMMv1.0: Pause functions
    //
    bool FPauseCore(WIT *pwit, int32_t *pdts); // 3DMMv1.0: Adds\Removes a pause to the current frame.
    bool FPause(WIT wit, int32_t dts);         // 3DMMv1.0: Adds\Removes a pause to the current frame, and undo

    //
    // 3DMMv1.0: Background functions
    //
    bool FSetBkgdCore(PTAG ptag, PTAG ptagOld); // 3DMMv1.0: Sets the background for this scene.
    bool FSetBkgd(PTAG ptag);                   // 3DMMv1.0: Sets the background for this scene, and undo
    BKGD *Pbkgd(void)
    {
        return _pbkgd;
    }                                                     // 3DMMv1.0: Gets the background for this scene.
    bool FChangeCamCore(int32_t icam, int32_t *picamOld); // 3DMMv1.0: Changes camera viewpoint at current frame.
    bool FChangeCam(int32_t icam);                        // 3DMMv1.0: Changes camera viewpoint at current frame, and undo
    PMBMP PmbmpThumbnail(void);                           // 3DMMv1.0: Returns the thumbnail.
    bool FGetTagBkgd(PTAG ptag);                          // 3DMMv1.0: Returns the tag for the background for this scene

    //
    // 3DMMv1.0: Movie functions
    //
    PMVIE Pmvie()
    {
        return (_pmvie);
    } // 3DMMv1.0: Get the parent movie

    //
    // 3DMMv1.0: Mark scene as dirty
    //
    void MarkDirty(bool fDirty = fTrue); // 3DMMv1.0: Mark the scene as changed.

    //
    // 3DMMv1.0: Clipboard type functions
    //
    bool FPasteActrCore(PACTR pactr, bool fInPlace = fFalse); // 3DMMv1.0: Pastes actor into current frame
    bool FPasteActr(PACTR pactr, bool fInPlace = fFalse);     // 3DMMv1.0: Pastes actor into current frame and undo

    //
    // 3DMMv1.0: Playing functions
    //
    bool FStartPlaying(void); // 3DMMv1.0: For special behavior when playback starts
    void StopPlaying(void);   // 3DMMv1.0: Used to clean up after playback has stopped.
};

#endif //! 3DMMv1.0: SCEN_H
