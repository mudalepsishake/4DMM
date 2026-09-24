/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *****************************************************************

 Actor class

 Primary Author : *****
 Review Status: Reviewed

 BASE -------> ACTR

 Each actor has a unique route which is defined by one or more nodes,
 and which may be a concatenation of one or more subroutes.
 Each actor also has an event list describing events which happen
 at a specified time and point along the route.  A point may coincide
 with a node or lie between nodes.

 *******************************************************************/
#ifndef ACTOR_H
#define ACTOR_H

//
// 3DMMv1.0:	XYZ : A point in x,y,z along an actor's RouTE.
//
struct XYZ
{
    BRS dxr;
    BRS dyr;
    BRS dzr;

    bool operator==(XYZ &xyz)
    {
        return ((dxr == xyz.dxr) && (dyr == xyz.dyr) && (dzr == xyz.dzr));
    }
    bool operator!=(XYZ &xyz)
    {
        return ((dxr != xyz.dxr) || (dyr != xyz.dyr) || (dzr != xyz.dzr));
    }
};
VERIFY_STRUCT_SIZE(XYZ, 12);
typedef XYZ *PXYZ;

const BOM kbomXyz = 0xfc000000;

//
// 3DMMv1.0: 	A RouTE is a general list (GL) of Route PoinTs(RPT)
// 3DMMv1.0:  A Subroute is a contiguous section of a route.
//
struct RPT
{
    XYZ xyz;
    BRS dwr; // 3DMMv1.0: Distance from this node to the next node on the route
};
VERIFY_STRUCT_SIZE(RPT, 16);
const BOM kbomRpt = 0xff000000;

const int32_t knfrmInvalid = klwMax;                                  // 3DMMv1.0: invalid frame state.  Regenerate correct state
const int32_t kcrptGrow = 32;                                         // 3DMMv1.0: quantum growth for rpt
const int32_t kcsmmGrow = 2;                                          // 3DMMv1.0: quantum growth for smm
const int32_t kctagSndGrow = 2;                                       // 3DMMv1.0: quantum growth for tagSnd
const int32_t smmNil = -1;                                            // 3DMMv1.0: Not motion match sound
const BRS kdwrNil = BR_SCALAR(-1.0);                                  // 3DMMv1.0: flags use of template cel stepsize
const BRS kzrDefault = BR_SCALAR(-25.0);                              // 3DMMv1.0: initial default z position
const BRS kdwrMax = BR_SCALAR(32767.0);                               // 3DMMv1.0: large BRS value
const uint32_t kdtsThreshRte = (kdtsSecond >> 2) + (kdtsSecond >> 1); // 3DMMv1.0: time threshhold before record in place
const BRS kdwrThreshRte = BR_SCALAR(2.0);                             // 3DMMv1.0: distance threshhold before entering record mode
const int32_t kcaevInit = 10;
const BRS kdwrFast = BR_SCALAR(3.0);       // 3DMMv1.0: delta world coord change for fast mouse move
const BRS krOriWeightMin = BR_SCALAR(0.1); // 3DMMv1.0: orientation weighting for slow mouse move
const BRS krAngleMin = BR_SCALAR(0.1);     // 3DMMv1.0: Min angle impacting amount of forward movement on placement
const BRS krAngleMinRcp = BR_RCP(krAngleMin);
const BRS krAngleMax = BR_SCALAR(0.4);         // 3DMMv1.0: Max angle impacting amount of forward movement on placement
const BRS krScaleMinFactor = (BR_SCALAR(0.1)); // 3DMMv1.0: Min scaling between Brender update
const auto krScaleMaxFactor = BrsDiv(rOne, krScaleMinFactor); // 3DMMv1.0: Max scaling between Brender update
const auto krScaleMinExtended = (BR_SCALAR(0.01));            // 4DMM experimental 100x-smaller floor
const auto krScaleMin = (BR_SCALAR(0.25));                    // original/default authoring floor
const auto krScaleMaxNormal = (BR_SCALAR(10.0));              // original/default authoring ceiling
const auto krScaleMax = (BR_SCALAR(100.0));                   // 4DMM experimental extended ceiling
const auto krPullMin = krScaleMin;
const auto krPullMax = BrsDiv(rOne, krPullMin);

#define aridNil ivNil

// 3DMMv1.0: Angle valid flags
enum
{
    fbraRotateX = factnRotateX,
    fbraRotateY = factnRotateY,
    fbraRotateZ = factnRotateZ
};

// 3DMMv1.0: Normalize flags
enum
{
    fnormSize = 1,
    fnormRotate = 2
};

// 3DMMv1.0: Mouse Actor flags
enum
{
    fmafNil = 0,
    fmafFreeze = 0x01,      // 3DMMv1.0: freeze cels
    fmafGround = 0x02,      // 3DMMv1.0: respect ground level
    fmafOrient = 0x04,      // 3DMMv1.0: orient actor during move
    fmafEntireScene = 0x08, // 3DMMv1.0: position over entire scene, vs subroute only
    fmafEntireSubrte = 0x10 // 3DMMv1.0: position over entire subroute
};

struct RTEL // 3DMMv1.0: RouTE Location - a function of space and time
{
    int irpt;      // 3DMMv1.0: The preceding node for the given point
    BRS dwrOffset; // 3DMMv1.0: Absolute linear distance beyond node irpt
    int32_t dnfrm; // 3DMMv1.0: Delta frame number (ie, time) at this point

    bool operator==(RTEL &rtel)
    {
        return (irpt == rtel.irpt && dwrOffset == rtel.dwrOffset && dnfrm == rtel.dnfrm);
    }

    bool operator!=(RTEL &rtel)
    {
        return (irpt != rtel.irpt || dwrOffset != rtel.dwrOffset || dnfrm != rtel.dnfrm);
    }

    bool operator<=(RTEL &rtel)
    {
        return (irpt < rtel.irpt || (irpt == rtel.irpt && (dwrOffset < rtel.dwrOffset ||
                                                           (dwrOffset == rtel.dwrOffset && dnfrm <= rtel.dnfrm))));
    }

    bool operator>=(RTEL &rtel)
    {
        return (irpt > rtel.irpt || (irpt == rtel.irpt && (dwrOffset > rtel.dwrOffset ||
                                                           (dwrOffset == rtel.dwrOffset && dnfrm >= rtel.dnfrm))));
    }

    bool operator<(RTEL &rtel)
    {
        return (irpt < rtel.irpt || (irpt == rtel.irpt && (dwrOffset < rtel.dwrOffset ||
                                                           (dwrOffset == rtel.dwrOffset && dnfrm < rtel.dnfrm))));
    }

    bool operator>(RTEL &rtel)
    {
        return (irpt > rtel.irpt || (irpt == rtel.irpt && (dwrOffset > rtel.dwrOffset ||
                                                           (dwrOffset == rtel.dwrOffset && dnfrm > rtel.dnfrm))));
    }
};

// 3DMMv1.0: Actor EVents are stored in a GG (general group)
// 3DMMv1.0: Fixed part of the GG:
struct AEV
{
    int32_t aet;  // 3DMMv1.0: Actor Event Type
    int32_t nfrm; // 3DMMv1.0: Absolute frame number (* Only valid < current event)
    RTEL rtel;    // 3DMMv1.0: RouTE Location for this event
};                // 3DMMv1.0: Additional event parameters (in the GG)
VERIFY_STRUCT_SIZE(AEV, 20);
typedef AEV *PAEV;

//
// 3DMMv1.0:	Actor level Event Types which live in a GG.
// 3DMMv1.0:	The fixed part of a GG entry is an actor event (aev)
// 3DMMv1.0:	The variable part is documented in the following comments
//
enum AET
{
    aetAdd,    // 3DMMv1.0: Add Actor onstage: aevadd - internal (no api)
    aetActn,   // 3DMMv1.0: Animate Actor : aevactn
    aetCost,   // 3DMMv1.0: Set Costume : aevcost
    aetRotF,   // 3DMMv1.0: Transform Actor rotate: BMAT34
    aetPull,   // 3DMMv1.0: Transform Actor Pull : aevpull
    aetSize,   // 3DMMv1.0: Transform Actor size uniformly : BRS
    aetSnd,    // 3DMMv1.0: Play a sound : aevsnd
    aetMove,   // 3DMMv1.0: Translate the path at this point : XYZ
    aetFreeze, // 3DMMv1.0: Freeze (or Un) Actor : long
    aetTweak,  // 3DMMv1.0: Path tweak : XYZ
    aetStep,   // 3DMMv1.0: Force step size (eg, float, wait) : BRS
    aetRem,    // 3DMMv1.0: Remove an actor from the stage : nil
    aetRotH,   // 3DMMv1.0: Single frame rotation : BMAT34
    aetLim
};
VERIFY_STRUCT_SIZE(AET, 4);

const BOM kbomAet = 0xc0000000;
const BOM kbomAev = 0xff000000;

//
// 3DMMv1.0:	Variable part of the Actor EVent GG:
//
struct AEVPULL // 3DMMv1.0: Squash/stretch
{
    BRS rScaleX;
    BRS rScaleY;
    BRS rScaleZ;
};
VERIFY_STRUCT_SIZE(AEVPULL, 12);
const BOM kbomAevpull = 0xfc000000;

// 3DMMv1.0: Every subroute is normalized.  The normalization translation is
// 3DMMv1.0: stored in the Add Event
// 3DMMv1.0: ** nfrmPrev valid for nfrmSub <= _nfrmCur only (optimization)
struct AEVADD
{
    BRS dxr; // 3DMMv1.0: Translation in x for this subroute
    BRS dyr; // 3DMMv1.0: Translation in y for this subroute
    BRS dzr; // 3DMMv1.0: Translation in z for this subroute
    BRA xa;  // 3DMMv1.0: Single point orientation
    BRA ya;  // 3DMMv1.0: Single point orientation
    BRA za;  // 3DMMv1.0: Single point orientation
};
VERIFY_STRUCT_SIZE(AEVADD, 20);
const BOM kbomAevadd = 0xffc00000 | kbomBmat34 >> 10;

struct AEVACTN
{
    int32_t anid;
    int32_t celn; // 3DMMv1.0: starting cel of action
};
VERIFY_STRUCT_SIZE(AEVACTN, 8);
const BOM kbomAevactn = 0xf0000000;

// 3DMMEx: On-disk representation of AEVCOST
struct AEVCOSTF
{
    int32_t ibset; // 3DMMv1.0: body part set
    int32_t cmid;  // 3DMMv1.0: costume ID (for custom costumes)
    tribool fCmtl; // 3DMMv1.0: vs fMtrl
    TAGF tag;
};
VERIFY_STRUCT_SIZE(AEVCOSTF, 28);

struct AEVCOST
{
    int32_t ibset; // 3DMMv1.0: body part set
    int32_t cmid;  // 3DMMv1.0: costume ID (for custom costumes)
    tribool fCmtl; // 3DMMv1.0: vs fMtrl
    TAG tag;
};
const BOM kbomAevcost = 0xfc000000 | (kbomTag >> 6);

// 3DMMEx: On-disk representation of AEVSND
struct AEVSNDF
{
    tribool fLoop;    // 3DMMv1.0: loop count
    tribool fQueue;   // 3DMMv1.0: queued sound
    int32_t vlm;      // 3DMMv1.0: volume
    int32_t celn;     // 3DMMv1.0: motion match	: ivNil if not
    int32_t sty;      // 3DMMv1.0: sound type
    tribool fNoSound; // 3DMMv1.0: no sound
    CHID chid;        // 3DMMv1.0: user sound requires chid
    TAGF tag;
};
VERIFY_STRUCT_SIZE(AEVSNDF, 44);

struct AEVSND
{
    tribool fLoop;    // 3DMMv1.0: loop count
    tribool fQueue;   // 3DMMv1.0: queued sound
    int32_t vlm;      // 3DMMv1.0: volume
    int32_t celn;     // 3DMMv1.0: motion match	: ivNil if not
    int32_t sty;      // 3DMMv1.0: sound type
    tribool fNoSound; // 3DMMv1.0: no sound
    CHID chid;        // 3DMMv1.0: user sound requires chid
    TAG tag;
};
const BOM kbomAevsnd = 0xfff00000 | (kbomTag >> 12);

const BOM kbomAevsize = 0xc0000000;
const BOM kbomAevfreeze = 0xc0000000;
const BOM kbomAevstep = 0xc0000000;
const BOM kbomAevmove = kbomXyz;
const BOM kbomAevtweak = kbomXyz;
const BOM kbomAevrot = kbomBmat34;

// 3DMMv1.0: Separate ggaev variable portion sizes
#define kcbVarAdd (SIZEOF(AEVADD))
#define kcbVarActn (SIZEOF(AEVACTN))
#define kcbVarCost (SIZEOF(AEVCOST))
#define kcbVarRot (SIZEOF(BMAT34))
#define kcbVarSize (SIZEOF(BRS))
#define kcbVarPull (SIZEOF(AEVPULL))
#define kcbVarSnd (SIZEOF(AEVSND))
#define kcbVarFreeze (SIZEOF(int32_t))
#define kcbVarMove (SIZEOF(XYZ))
#define kcbVarTweak (SIZEOF(XYZ))
#define kcbVarStep (SIZEOF(BRS))
#define kcbVarZero (0)

// 3DMMv1.0: Actor Event Flags
enum
{
    faetNil = 0,
    faetAdd = 1 << aetAdd,
    faetActn = 1 << aetActn,
    faetCost = 1 << aetCost,
    faetRotF = 1 << aetRotF,
    faetPull = 1 << aetPull,
    faetSize = 1 << aetSize,
    faetFreeze = 1 << aetFreeze,
    faetTweak = 1 << aetTweak,
    faetStep = 1 << aetStep,
    faetRem = 1 << aetRem,
    faetMove = 1 << aetMove,
    faetRotH = 1 << aetRotH
};

//
// 3DMMv1.0: Because rotations are non-abelian, cumulative rotations are stored
// 3DMMv1.0: in a BMAT34.  It would not be correct to multiply (xa,ya,za) values
// 3DMMv1.0: treating x,y,z independently.
//
// 3DMMv1.0: Bmat34Fwd does NOT include the path portion of the orientation.
// 3DMMv1.0: Bmat34Cur includes all current angular rotation - path rotation is NOT post
// 3DMMv1.0: applied to this.
//
// 3DMMv1.0: Careful: The task is complicated by the fact that users can apply single
// 3DMMv1.0: frame and frame forward orientations in the same frame and must not see the
// 3DMMv1.0: actor jump in angle when choosing between the two methods of editing.
//
struct XFRM
{
    BMAT34 bmat34Fwd; // 3DMMv1.0: Rotation	fwd	: path rotation post applied to this
    AEVPULL aevpull;  // 3DMMv1.0: Stretching (pulling) constants
    BRS rScaleStep;   // 3DMMv1.0: Uniform scaling to be applied to step size
    BMAT34 bmat34Cur; // 3DMMv1.0: <<Path independent>> Single frame & static segment rotation
    BRA xaPath;       // 3DMMv1.0: Path portion of the current frame's rotation
    BRA yaPath;       // 3DMMv1.0: Path portion of the current frame's rotation
    BRA zaPath;       // 3DMMv1.0: Path portion of the current frame's rotation
};

//
// 3DMMv1.0: Current action Motion Match Sounds
// 3DMMv1.0: Aev & Aevsnd grouped together form a gl
// 3DMMv1.0: Not to be saved with a movie
//
struct SMM
{
    AEV aev; // 3DMMv1.0: event for the sound
    AEVSND aevsnd;
};

//
// 3DMMv1.0: Default hilite colors
//
#define kiclrNormalHilite 108
#define kiclrTimeFreezeHilite 43

/** 3DMMv1.0: *********************************************
   Actor Class
***********************************************/
typedef class ACTR *PACTR;
#define ACTR_PAR BASE
#define kclsACTR KLCONST4('A', 'C', 'T', 'R')
class ACTR : public ACTR_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // 3DMMv1.0: Components of an actor
    // 3DMMv1.0: Note: In addition to these components, any complete actor must
    // 3DMMv1.0: have either fLifeDirty set or _nfrmLast current.
    // 3DMMv1.0: Note: _tagTmpl cannot be derived from _ptmpl
    PGG _pggaev;        // 3DMMv1.0: GG pointer to Actor EVent list
    PGL _pglrpt;        // 3DMMv1.0: GL pointer to actor's route
    TMPL *_ptmpl;       // 3DMMv1.0: Actor body & action list template
    BODY *_pbody;       // 3DMMv1.0: Actor's body
    TAG _tagTmpl;       // 3DMMv1.0: Note: The sid cannot be queried at save time
    TAG _tagSnd;        // 3DMMv1.0: Sound (played on entrance)
    SCEN *_pscen;       // 3DMMv1.0: Underlying scene
    XYZ _dxyzFullRte;   // 3DMMv1.0: Origin of the route
    int32_t _nfrmFirst; // 3DMMv1.0: klwMax -or- First frame : Set	when event created
    int32_t _arid;      // 3DMMv1.0: Unique id assigned to this actor.
    uint32_t _grfactn;  // 3DMMv1.0: Cached current grfactn

    // 3DMMv1.0: Frame Dependent State Information
    XYZ _dxyzRte;            // 3DMMv1.0: _dxyzFullRte + _dxyzSubRte : Set when Add processed
    XYZ _dxyzSubRte;         // 3DMMv1.0: Subpath translation : Set when Add processed
    bool _fOnStage : 1;      // 3DMMv1.0: Versus Brender hidden.  Set by Add, Rem only
    bool _fFrozen : 1;       // 3DMMv1.0: Path offset > 0 but not moving
    bool _fLifeDirty : 1;    // 3DMMv1.0: Set if _nfrmLast requires recomputation
    bool _fPrerendered : 1;  // 3DMMv1.0: Set if actor is prerendered
    bool _fUseBmat34Cur : 1; // 3DMMv1.0: _xfrm.bmat34Cur last used in orienting actor
    BRS _dwrStep;            // 3DMMv1.0: Current step size in use
    int32_t _anidCur;        // 3DMMv1.0: Current action in template
    int32_t _ccelCur;        // 3DMMv1.0: Cached cel count.  Retrieving this was hi profile.
    int32_t _celnCur;        // 3DMMv1.0: Current cell
    int32_t _nfrmCur;        // 3DMMv1.0: Current frame number : Set by FGotoFrame
    int32_t _nfrmLast;       // 3DMMv1.0: Latest known frame for this actor.
    int32_t _iaevCur;        // 3DMMv1.0: Current event in event list
    int32_t _iaevFrmMin;     // 3DMMv1.0: First event in current frame
    int32_t _iaevActnCur;    // 3DMMv1.0: Event defining current action
    int32_t _iaevAddCur;     // 3DMMv1.0: Most recent add (useful for Compose)
    RTEL _rtelCur;           // 3DMMv1.0: Current location on route	(excludes tweak info)
    XYZ _xyzCur;             // 3DMMv1.0: Last point displayed (may be tweak modified)
    XFRM _xfrm;              // 3DMMv1.0: Current transformation
    PGL _pglsmm;             // 3DMMv1.0: Current action motion match sounds

    // 3DMMv1.0: Path Recording State Information
    RTEL _rtelInsert;        // 3DMMv1.0: Joining information
    uint32_t _tsInsert;      // 3DMMv1.0: Starting time of route recording
    bool _fModeRecord : 1;   // 3DMMv1.0: Record a route mode
    bool _fRejoin : 1;       // 3DMMv1.0: Rerecord is extending a subpath from the end
    bool _fPathInserted : 1; // 3DMMv1.0: More path inserted
    bool _fTimeFrozen : 1;   // 3DMMv1.0: Is the actor frozen wrt time?
    int32_t _dnfrmGap;       // 3DMMv1.0: Frames between subroutes
    XYZ _dxyzRaw;            // 3DMMv1.0: Raw mouse movement from previous frame

    //
    // 3DMMv1.0:	Protected functions
    //
    ACTR(void);
    bool _FInit(TAG *ptmplTag);         // 3DMMv1.0: Constructor allocation & file I/O
    void _InitXfrmRot(BMAT34 *pbmat34); // 3DMMv1.0: Initialize rotation only
    void _InitXfrm(void);               // 3DMMv1.0: Initialize rotation & scaling
    bool _FCreateGroups(void);
    void _InitState(void);
    void _GetNewOrigin(BRS *pxr, BRS *pyr, BRS *pzr);
    void _SetStateRewound(void);

    bool _FQuickBackupToFrm(int32_t nfrm, bool *pfQuickMethodValid);
    bool _FGetRtelBack(RTEL *prtel, bool fUpdateStateVar);
    bool _FDoFrm(bool fPositionBody, bool *pfPositionDirty, bool *pfSoundInFrame = pvNil);
    bool _FGetStatic(int32_t anid, bool *pfStatic);
    bool _FIsDoneAevSub(int32_t iaev, RTEL rtel);
    bool _FIsAddNow(int32_t iaev);
    bool _FGetDwrPlay(BRS *pdwr);   // 3DMMv1.0: Step size if playing
    bool _FGetDwrRecord(BRS *pdwr); // 3DMMv1.0: Step size if recording
    bool _FDoAevCur(void);
    bool _FDoAevCore(int32_t iaev);
    bool _FDoAetVar(int32_t aet, void *pvVar, int32_t cbVar);

    bool _FEnqueueSnd(int32_t iaev);
    bool _FEnqueueSmmInMsq(void);
    bool _FInsertSmm(int32_t iaev);
    bool _FRemoveAevMm(int32_t anid);
    bool _FAddAevDefMm(int32_t anid);

    bool _FAddDoAev(int32_t aetNew, int32_t kcbNew, void *pvVar);
    void _MergeAev(int32_t iaevFirst, int32_t iaevLast, int32_t *piaevNew = pvNil);
    bool _FFreeze(void);   // 3DMMv1.0: insert freeze event
    bool _FUnfreeze(void); // 3DMMv1.0: insert unfreeze event
    void _Hide(void);
    bool _FInsertGgRpt(int32_t irpt, RPT *prpt, BRS dwrPrior = rZero);
    bool _FAddAevFromPrev(int32_t iaevLim, uint32_t grfaet);
    bool _FAddAevFromLater(void);
    bool _FFindNextAevAet(int32_t aet, int32_t iaevCur, int32_t *piaevAdd);
    bool _FFindPrevAevAet(int32_t aet, int32_t iaevCur, int32_t *piaevAdd);
    void _FindAevLastSub(int32_t iaevAdd, int32_t iaevLim, int32_t *piaevLast);
    void _DeleteFwdCore(bool fDeleteAll, bool *pfAlive = pvNil, int32_t iaevCur = ivNil);
    bool _FDeleteEntireSubrte(void);
    void _DelAddFrame(int32_t iaevAdd, int32_t iaevLim);

    void _UpdateXyzRte(void);
    bool _FInsertAev(int32_t iaev, int32_t cbNew, void *pvVar, void *paev, bool fUpdateState = fTrue);
    void _RemoveAev(int32_t iaev, bool fUpdateState = fTrue);
    void _PrepXfrmFill(int32_t aet, void *pvVar, int32_t cbVar, int32_t iaevMin, int32_t iaevCmp = ivNil,
                       uint32_t grfaet = faetNil);
    void _PrepActnFill(int32_t iaevMin, int32_t anidPrev, int32_t anidNew, uint32_t grfaet);
    void _PrepCostFill(int32_t iaevMin, AEVCOST *paevcost);
    void _AdjustAevForRteIns(int32_t irptAdjust, int32_t iaevMin);
    void _AdjustAevForRteDel(int32_t irptAdjust, int32_t iaevMin);
    bool _FInsertStop(void);
    void _CalcRteOrient(BMAT34 *pbmat34, BRA *pxa = pvNil, BRA *pya = pvNil, BRA *pza = pvNil,
                        uint32_t *pgrfbra = pvNil);
    void _ApplyRotFromVec(XYZ *pxyz, BMAT34 *pbmat34, BRA *pxa = pvNil, BRA *pya = pvNil, BRA *pza = pvNil,
                          uint32_t *grfbra = pvNil);
    void _SaveCurPathOrien(void);
    void _LoadAddOrien(AEVADD *paevadd, bool fNoReset = fFalse);
    BRA _BraAvgAngle(BRA a1, BRA a2, BRS rw);
    void _UpdateXyzTan(XYZ *pxyz, int32_t irptTan, int32_t rw);

    void _AdvanceRtel(BRS dwrStep, RTEL *prtel, int32_t iaevCur, int32_t nfrmCur, bool *pfEndRoute);
    void _GetXyzFromRtel(RTEL *prtel, PXYZ pxyz);
    void _GetXyzOnLine(PXYZ pxyzFirst, PXYZ pxyzSecond, BRS dwrOffset, PXYZ pxyz);
    void _PositionBody(PXYZ pxyz);
    void _MatrixRotUpdate(XYZ *pxyz, BMAT34 *pbmat34);
    void _TruncateSubRte(int32_t irptDelLim);
    bool _FComputeLifetime(int32_t *pnfrmLast = pvNil);
    bool _FIsStalled(int32_t iaevFirst, RTEL *prtel, int32_t *piaevLast = pvNil);

    void _RestoreFromUndo(PACTR pactrRestore);
    bool _FDupCopy(PACTR pactrSrc, PACTR pactrDest);

    bool _FWriteTmpl(PCFL pcfl, CNO cno);
    bool _FReadActor(PCFL pcfl, CNO cno);
    bool _FReadRoute(PCFL pcfl, CNO cno);
    bool _FReadEvents(PCFL pcfl, CNO cno);
    bool _FOpenTags(PCRF pcrf);
    static bool _FIsIaevTag(PGG pggaev, int32_t iaev, PTAG *pptag, PAEV *pqaev = pvNil);
    void _CloseTags(void);

  public:
    ~ACTR(void);
    static PACTR PactrNew(TAG *ptagTmpl);
    void SetPscen(SCEN *pscen);
    void SetArid(int32_t arid)
    {
        AssertBaseThis(0);
        _arid = arid;
    }
    void SetLifeDirty(void)
    {
        AssertBaseThis(0);
        _fLifeDirty = fTrue;
    }
    PSCEN Pscen(void)
    {
        AssertThis(0);
        return _pscen;
    }

    // 3DMMv1.0: ActrSave Routines
    static PACTR PactrRead(PCRF pcrf, CNO cno);    // 3DMMv1.0: Construct from a document
    bool FWrite(PCFL pcfl, CNO cno, CNO cnoScene); // 3DMMv1.0: Write to a document
    static PGL PgltagFetch(PCFL pcfl, CNO cno, bool *pfError);
    static bool FAdjustAridOnFile(PCFL pcfl, CNO cno, int32_t darid);

    // 3DMMv1.0: Visibility
    void Hilite(void);
    void Unhilite(void)
    {
        AssertBaseThis(0);
        _pbody->Unhilite();
    }
    void Hide(void)
    {
        AssertBaseThis(0);
        _pbody->Hide();
    }
    void Show(void)
    {
        AssertBaseThis(0);
        _pbody->Show();
    }
    bool FIsInView(void)
    {
        AssertBaseThis(0);
        return _pbody->FIsInView();
    }
    void GetCenter(int32_t *pxp, int32_t *pyp)
    {
        AssertBaseThis(0);
        _pbody->GetCenter(pxp, pyp);
    }
    void GetRcBounds(RC *prc)
    {
        AssertBaseThis(0);
        _pbody->GetRcBounds(prc);
    }
    void SetPrerendered(bool fPrerendered)
    {
        AssertBaseThis(0);
        _fPrerendered = fPrerendered;
    }
    bool FPrerendered(void)
    {
        AssertBaseThis(0);
        return _fPrerendered;
    }

    // 3DMMv1.0: Actor Information
    int32_t Arid(void)
    {
        AssertBaseThis(0);
        return (_arid);
    }
    bool FOnStage(void)
    {
        AssertBaseThis(0);
        return (_fOnStage);
    }
    bool FIsMyBody(BODY *pbody)
    {
        AssertBaseThis(0);
        return pbody == _pbody;
    }
    bool FIsMyTmpl(TMPL *ptmpl)
    {
        AssertBaseThis(0);
        return _ptmpl == ptmpl;
    }
    bool FIsModeRecord(void)
    {
        AssertBaseThis(0);
        return FPure(_fModeRecord);
    }
    bool FIsRecordValid(BRS dxr, BRS dyr, BRS dzr, uint32_t tsCurrent);
    PTMPL Ptmpl(void)
    {
        AssertBaseThis(0);
        return _ptmpl;
    }
    PBODY Pbody(void)
    {
        AssertBaseThis(0);
        return _pbody;
    }
    int32_t AnidCur(void)
    {
        AssertBaseThis(0);
        return _anidCur;
    }
    int32_t CelnCur(void)
    {
        AssertBaseThis(0);
        return _celnCur;
    }
    bool FFrozen(void)
    {
        AssertBaseThis(0);
        return _fFrozen;
    }
    bool FGetLifetime(int32_t *pnfrmFirst, int32_t *pnfrmLast); // 3DMMv1.0: allows nil ptrs
    bool FPtIn(int32_t xp, int32_t yp, int32_t *pibset);
    bool FUsesLargeScalePickFallback(void)
    {
        AssertBaseThis(0);
        return _xfrm.rScaleStep > BR_SCALAR(10.0);
    }
    void GetTagTmpl(PTAG ptag)
    {
        AssertBaseThis(0);
        *ptag = _tagTmpl;
    }
    void GetName(PSTN pstn);
    bool FChangeTagTmpl(PTAG ptagTmplNew);
    bool FRefreshBodyForTemplateMutation(void);
    bool FTimeFrozen(void)
    {
        AssertBaseThis(0);
        return _fTimeFrozen;
    }
    void SetTimeFreeze(bool fTimeFrozen)
    {
        AssertBaseThis(0);
        _fTimeFrozen = fTimeFrozen;
    }
    bool FIsPropBrws(void)
    {
        AssertThis(0);
        return FPure(_ptmpl->FIsProp() || _ptmpl->FIsTdt());
    }
    bool FIsTdt(void)
    {
        AssertThis(0);
        return FPure(_ptmpl->FIsTdt());
    }
    bool FMustRender(int32_t nfrmRenderLast);
    void GetXyzWorld(BRS *pxr, BRS *pyr, BRS *pzr);
    void GetObjectGroupPose(BRS *pxr, BRS *pyr, BRS *pzr, BMAT34 *pbmat34);
    bool FSetObjectGroupOrientation(const BMAT34 *pbmat34, bool fFromHereFwd);

    // 3DMMv1.0: Animation
    bool FGotoFrame(int32_t nfrm, bool *pfSoundInFrame = pvNil); // 3DMMv1.0: Prepare for display at frame nfrm
    bool FInsertHeldFramesAfter(int32_t nfrm, int32_t cfrm); // Insert held time after frame nfrm.
    bool FReplayFrame(int32_t grfscen);                          // 3DMMv1.0: Replay a frame.

    // 3DMMv1.0: Event Editing
    bool FAddOnStageCore(void);
    bool FSetActionCore(int32_t anid, int32_t celn, bool fFreeze);
    bool FRemFromStageCore(void);
    bool FSetCostumeCore(int32_t ibsetClicked, TAG *ptag, int32_t cmid, tribool fCustom);
    bool FSetStep(BRS dwrStep);
    bool FRotate(BRA xa, BRA ya, BRA za, bool fFromHereFwd);
    bool FNormalizeCore(uint32_t grfnorm);
    void SetAddOrient(BRA xa, BRA ya, BRA za, uint32_t grfbra, XYZ *pdxyz = pvNil);
    bool FScale(BRS rScaleStep, BRS rScaleMin = krScaleMin, BRS rScaleMax = krScaleMaxNormal);
    bool FPull(BRS rScaleX, BRS rScaleY, BRS rScaleZ);
    void DeleteFwdCore(bool fDeleteAll, bool *pfAlive = pvNil, int32_t iaevCur = ivNil);
    void DeleteBackCore(bool *pfAlive = pvNil);
    bool FSoonerLater(int32_t dnfrm);

    // 3DMMv1.0: ActrEdit Routines
    bool FDup(PACTR *ppactr, bool fReset = fFalse); // 3DMMv1.0: Duplicate everything
    void Restore(PACTR pactr);

    bool FCreateUndo(PACTR pactr, bool fSndUndo = fFalse, PSTN pstn = pvNil); // 3DMMv1.0: Create undo object
    void Reset(void);
    bool FAddOnStage(void); // 3DMMv1.0: add actor to the stage, w/Undo
    bool FSetAction(int32_t anid, int32_t celn, bool fFreeze, PACTR *ppactrDup = pvNil);
    bool FSetCostume(int32_t ibset, TAG *ptag, int32_t cmid, tribool fCustom);
    bool FRemFromStage(void);                                 // 3DMMv1.0: add event: rem actor from stage, w/Undo
    bool FCopy(PACTR *ppactr, bool fEntireScene = fFalse);    // 3DMMv1.0: Duplicate actor from this frame on
    bool FCopyRte(PACTR *ppactr, bool fEntireScene = fFalse); // 3DMMv1.0: Duplicate path from this frame on
    bool FPasteRte(PACTR pactr);                              // 3DMMv1.0: Paste from clipboard from this frame on
    bool FNormalize(uint32_t grfnorm);
    bool FPaste(int32_t nfrm, SCEN *pscen, bool fInPlace = fFalse);
    bool FDelete(bool *pfAlive, bool fDeleteAll);

    // 3DMMv1.0: ActrSnd Routines
    bool FSetSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fActnCel, int32_t vlm, int32_t sty);
    bool FSetSndCore(PTAG ptag, tribool fLoop, tribool fQueue, tribool fActnCel, int32_t vlm, int32_t sty);
    bool FSetVlmSnd(int32_t sty, bool fMotionMatch, int32_t vlm); // 3DMMv1.0: Set the volume of a sound
    bool FQuerySnd(int32_t sty, bool fMotionMatch, PGL *pglTagSnd, int32_t *pvlm, bool *pfLoop);
    bool FDeleteSndCore(int32_t sty, bool fMotionMatch);
    bool FSoundInFrm(void);
    bool FResolveAllSndTags(CNO cnoScen);

    // 3DMMv1.0: Route Definition
    void SetTsInsert(uint32_t tsCurrent)
    {
        AssertBaseThis(0);
        _tsInsert = tsCurrent;
    }
    bool FBeginRecord(uint32_t tsCurrent, bool fReplace, PACTR pactrRestore);
    bool FRecordMove(BRS dxr, BRS dyr, BRS dzr, uint32_t grfmaf, uint32_t tsCurrent, bool *pfLonger, bool *pfStep,
                     PACTR pactrRestore);
    bool FEndRecord(bool fReplace, PACTR pactrRestore);
    bool FTweakRoute(BRS dxr, BRS dyr, BRS dzr, uint32_t grfmaf = fmafNil);
    bool FMoveRoute(BRS dxr, BRS dyr, BRS dzr, bool *pfMoved = pvNil, uint32_t grfmaf = fmafNil);
};

//
// 3DMMv1.0: Actor document for clipping
//
typedef class ACLP *PACLP;
#define ACLP_PAR DOCB
#define kclsACLP KLCONST4('A', 'C', 'L', 'P')
class ACLP : public ACLP_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    PACTR _pactr;
    bool _fRteOnly;
    STN _stnName;

    // 4DMM: clipboard copies carry the complete attached Light Lab settings.
    // Keep primitives here instead of LIGHTLAB itself so actor.h does not
    // acquire a circular dependency on movie.h.
    bool _fHasLightLab;
    bool _fLightEnabled;
    bool _fLightGenerateShadows;
    bool _fLightAttachmentHideable;
    int32_t _lightIntensity;
    float _lightEdgeGradient;
    float _lightDiameter;
    float _lightRange;
    achar _szLightShape[16];

    // v238: clipboard copies also carry any non-default Object Properties so
    // repeated props/actors/3D Words do not need to be reconfigured by hand.
    bool _fHasObjectProperties;
    bool _fObjectFlushOverlap;
    bool _fObjectCastShadows;

    ACLP(void)
    {
        _pactr = pvNil;
        _fRteOnly = fFalse;
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
    //
    // 3DMMv1.0: Constructors and destructors
    //
    static PACLP PaclpNew(PACTR pactr, bool fRteOnly, bool fEndScene = fFalse);
    ~ACLP(void);

    //
    // 3DMMv1.0: Pasting function
    //
    bool FPaste(PMVIE pmvie);

    bool FRouteOnly(void)
    {
        return _fRteOnly;
    }
};

#endif //! 3DMMv1.0: ACTOR_H
