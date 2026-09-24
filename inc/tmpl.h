/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: ***********************************************************************

    tmpl.h: Actor template class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BACO ---> ACTN
    BASE ---> BACO ---> TMPL

    A TMPL encapsulates all the data that distinguishes one actor
    "species" from another, including the species' models, actions,
    and custom texture maps.  One or more BODY classes are created based
    on a TMPL, and the TMPL attaches models and materials to the body
    based on more abstract concepts like actions and costumes.

*************************************************************************/
#ifndef TMPL_H
#define TMPL_H

/** 3DMMv1.0: **************************************
    Cel part spec: tells what model and
    xfrm to apply to a body part for
    one cel
****************************************/
struct CPS
{
    int16_t chidModl; // 3DMMv1.0: CHID (under TMPL chunk) of model for this body part
    int16_t imat34;   // 3DMMv1.0: index into ACTN's GL of transforms
};
VERIFY_STRUCT_SIZE(CPS, 4);
const BOM kbomCps = 0x50000000;

// 4DMM Actor Studio temporal BODY-part absence. Ordinary TMPL model CHIDs
// occupy the low 15 bits (custom-part creation already rejects > 0x7fff).
// Preserve that model identity in the CPS while the part is cut by setting the
// sign/high bit. chidNil keeps its historical accessory meaning and is never
// encoded through these helpers. This makes Cut reversible after save/reopen
// without a parallel side table and without destroying BODY topology.
const uint16_t kgrf4DMMActorStudioHiddenModel = 0x8000;
inline bool F4DMMActorStudioCpsModelHidden(int16_t chidModl)
{
    return chidModl != chidNil &&
           ((((uint16_t)chidModl) & kgrf4DMMActorStudioHiddenModel) != 0);
}
inline int16_t Chid4DMMActorStudioHideModel(int16_t chidModl)
{
    return (int16_t)(((uint16_t)chidModl) | kgrf4DMMActorStudioHiddenModel);
}
inline int16_t Chid4DMMActorStudioShowModel(int16_t chidModl)
{
    return (int16_t)(((uint16_t)chidModl) & ~kgrf4DMMActorStudioHiddenModel);
}

/** 3DMMv1.0: **************************************
    Cel: tells what CPS's to apply to an
    actor for one cel.  It also tells
    what sound to play (if any), and how
    far the actor should move from the
    previous cel (dwr).
****************************************/
struct CEL
{
    CHID chidSnd; // 3DMMv1.0: sound to play at this cel (CHID under ACTN chunk)
    BRS dwr;      // 3DMMv1.0: distance from previous cel
                  // 3DMMv1.0:	CPS rgcps[];	// list of cel part specs (variable part of pggcel)
};
VERIFY_STRUCT_SIZE(CEL, 8);
const BOM kbomCel = 0xf0000000;

// 3DMMv1.0: template on file
struct TMPLF
{
    int16_t bo;
    int16_t osk;
    BRA xaRest; // 3DMMv1.0: reminder: BRAs are shorts
    BRA yaRest;
    BRA zaRest;
    int16_t swPad; // 3DMMv1.0: so grftmpl (and the whole TMPLF) is long-aligned
    uint32_t grftmpl;
};
VERIFY_STRUCT_SIZE(TMPLF, 16);
#define kbomTmplf 0x554c0000

// 3DMMv1.0: action chunk on file
struct ACTNF
{
    int16_t bo;
    int16_t osk;
    int32_t grfactn;
};
VERIFY_STRUCT_SIZE(ACTNF, 8);
const uint32_t kbomActnf = 0x5c000000;

// 3DMMv1.0: grfactn flags
enum
{
    factnRotateX = 1, // 3DMMv1.0: Tells whether actor should rotate around this
    factnRotateY = 2, // 3DMMv1.0:   axis when following a path
    factnRotateZ = 4,
    factnStatic = 8, // 3DMMv1.0: Tells whether this is a stationary action
};

/** 3DMMv1.0: **************************************
    ACTN (action): all the information
    for an action like 'rest' or 'walk'.
****************************************/
typedef class ACTN *PACTN;
#define ACTN_PAR BACO
#define kclsACTN KLCONST4('A', 'C', 'T', 'N')
class ACTN : public ACTN_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PGG _pggcel;       // 3DMMv1.0: GG of CELs; variable part is a rgcps[]
    PGL _pglbmat34;    // 3DMMv1.0: GL of transformation matrices used in this action
    PGL _pgltagSnd;    // 3DMMv1.0: GL of motion-match sounds for this action
    uint32_t _grfactn; // 3DMMv1.0: various flags for this action

  protected:
    ACTN(void)
    {
    } // 3DMMv1.0: can't instantiate directly; must use FReadActn
    bool _FInit(PCFL pcfl, CTG ctg, CNO cno);

  public:
    static PACTN PactnNew(PGG pggcel, PGL pglbmat34, uint32_t grfactn);
    static bool FReadActn(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);
    ~ACTN(void);

    uint32_t Grfactn(void)
    {
        return _grfactn;
    }

    int32_t Ccel(void)
    {
        return _pggcel->IvMac();
    }
    void GetCel(int32_t icel, CEL *pcel);
    void GetCps(int32_t icel, int32_t icps, CPS *pcps);
    void GetMatrix(int32_t imat34, BMAT34 *pbmat34);
    void GetSnd(int32_t icel, PTAG ptagSnd);
};

// 3DMMv1.0: grftmpl flags
enum
{
    ftmplOnlyCustomCostumes = 1, // 3DMMv1.0: fTrue means don't apply generic MTRLs
    ftmplTdt = 2,                // 3DMMv1.0: fTrue means this is a 3-D Text object
    ftmplProp = 4,               // 3DMMv1.0: fTrue means this is a "prop" actor
};

/** 3DMMv1.0: **************************************
    TMPL: The template class.
    anid is an action ID.
    cmid is a costume ID.
    celn is a cel number.
****************************************/
typedef class TMPL *PTMPL;
#define TMPL_PAR BACO
#define kclsTMPL KLCONST4('T', 'M', 'P', 'L')
class TMPL : public TMPL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    BRA _xaRest; // 3DMMv1.0: Rest orientation
    BRA _yaRest;
    BRA _zaRest;
    uint32_t _grftmpl;
    PGL _pglibactPar; // 3DMMv1.0: GL of parent IDs (shorts) to build BODY
    PGL _pglibset;    // 3DMMv1.0: GL of body-part-set IDs to build BODY
    PGG _pggcmid;     // 3DMMv1.0: List of costumes for each body part set
    int32_t _ccmid;   // 3DMMv1.0: Count of custom costumes
    int32_t _cbset;   // 3DMMv1.0: Count of body part sets
    int32_t _cactn;   // 3DMMv1.0: Count of actions
    STN _stn;         // 3DMMv1.0: Template name

  protected:
    TMPL(void)
    {
    } // 3DMMv1.0: can't instantiate directly; must use FReadTmpl
    bool _FReadTmplf(PCFL pcfl, CTG ctg, CNO cno);
    virtual bool _FInit(PCFL pcfl, CTG ctgTmpl, CNO cnoTmpl);
    virtual PACTN _PactnFetch(int32_t anid);
    virtual PMODL _PmodlFetch(CHID chidModl);
    bool _FWriteTmplf(PCFL pcfl, CTG ctg, CNO *pcno);

  public:
    static bool FReadTmpl(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);
    ~TMPL(void);
    static PGL PgltagFetch(PCFL pcfl, CTG ctg, CNO cno, bool *pfError);

    // 3DMMv1.0: TMPL / BODY stuff
    void GetName(PSTN pstn); // 3DMMv1.0: default name of actor or text of the TDT
    PBODY PbodyCreate(void); // 3DMMv1.0: Creates a body based on this TMPL
    bool FConformBodyShape(PBODY pbody); // 4DMM: rebuild BODY topology after custom-part edits
    bool FRefreshPartShape(void); // reload writable BODY topology/costume maps after structural edits
    void GetRestOrien(BRA *pxa, BRA *pya, BRA *pza);
    bool FIsTdt(void)
    {
        return FPure(_grftmpl & ftmplTdt);
    }
    bool FIsProp(void)
    {
        return FPure(_grftmpl & ftmplProp);
    }

    // 3DMMv1.0: Action stuff
    int32_t Cactn(void)
    {
        return _cactn;
    } // 3DMMv1.0: count of actions
    void RefreshActionCount(void); // rescan ACTN child CHIDs after Actor Studio Save As
    virtual bool FGetActnName(int32_t anid, PSTN pstn);
    bool FSetActnCel(PBODY pbody, int32_t anid, int32_t celn, BRS *pdwr = pvNil);
    bool FGetGrfactn(int32_t anid, uint32_t *pgrfactn);
    bool FGetDwrActnCel(int32_t anid, int32_t celn, BRS *pdwr);
    bool FGetCcelActn(int32_t anid, int32_t *pccel);
    bool FGetSndActnCel(int32_t anid, int32_t celn, bool *pfSoundExists, PTAG ptag);

    // 3DMMv1.0: Costume stuff
    virtual bool FSetDefaultCost(PBODY pbody); // 3DMMv1.0: applies default costume
    virtual PCMTL PcmtlFetch(int32_t cmid);
    int32_t CcmidOfBset(int32_t ibset);
    int32_t CmidOfBset(int32_t ibset, int32_t icmid);
    bool FBsetIsAccessory(int32_t ibset); // 3DMMv1.0: whether ibset holds accessories
    bool FIbsetAccOfIbset(int32_t ibset, int32_t *pibsetAcc);
    bool FSameAccCmids(int32_t cmid1, int32_t cmid2);
};

#endif // 3DMMEx: TMPL_H
