/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

  scene.cpp

  Author: Sean Selitrennikoff

  Date: August, 1994

  This file contains all functionality for scene manipulation.

  THIS IS A CODE REVIEWED FILE

    Basic scene private classes:

        Scene Chop Undo Object (SUNC)

            BASE ---> UNDB ---> MUNB ---> SUNC

        Scene Background Undo Object (SUNK)

            BASE ---> UNDB ---> MUNB ---> SUNK

        Scene Pause Undo Object (SUNP)

            BASE ---> UNDB ---> MUNB ---> SUNP

        Scene Text box Undo Object (SUNX)

            BASE ---> UNDB ---> MUNB ---> SUNX

        Scene Sound Undo Object (SUNS)

            BASE ---> UNDB ---> MUNB ---> SUNS

        Scene Title Undo Object (SUNT)

            BASE ---> UNDB ---> MUNB ---> SUNT

***************************************************************************/

#include "soc.h"
ASSERTNAME

#if defined(KAUAI_WIN32)
extern void Queue4DMMObjectGroupsSelectionRefresh(void);
#define REFRESH_4DMM_OBJECT_GROUP_SELECTION_UI() Queue4DMMObjectGroupsSelectionRefresh()
#else
#define REFRESH_4DMM_OBJECT_GROUP_SELECTION_UI() ((void)0)
#endif

#if defined(BRENDER_MODERN_14)
#define MODERN_BR_SCENE_LOG(...) \
    do \
    { \
        if (FBrModernLogEnabled()) \
            BrModernLog(__VA_ARGS__); \
    } while (0)
#else
#define MODERN_BR_SCENE_LOG(...) ((void)0)
#endif

//
// 3DMMv1.0: Scene event types
//
enum SEVT
{                   // 3DMMv1.0: StartEv	FrmEv	Param
    sevtAddActr,    // 3DMMv1.0:    X		  		pactr/chid
    sevtPlaySnd,    // 3DMMv1.0: 	  		  X		SSE (Scene Sound Event)
    sevtAddTbox,    // 3DMMv1.0: 	  X				ptbox/chid
    sevtChngCamera, // 3DMMv1.0: 			  X		icam
    sevtSetBkgd,    // 3DMMv1.0: 	  X				Background Tag
    sevtPause,      // 3DMMv1.0: 			  X		type, duration
    sevtBlankFrame  // 			  X		no variable data
};

//
// 3DMMv1.0: Struct for saving event pause information
//
struct SEVP
{
    WIT wit;
    int32_t dts;
};

//
// 3DMMv1.0: Scene thumbnails
//
const auto kdxpThumbnail = 144;
const auto kdypThumbnail = 81;
const auto kbTransparent = 250;

//
// 3DMMv1.0: Scene event
//
struct SEV
{
    int32_t nfrm; // 3DMMv1.0: frame number of the event.
    SEVT sevt;    // 3DMMv1.0: event type
};

const auto kbomSev = 0xF0000000;
const auto kbomLong = 0xC0000000;

//
// 3DMMv1.0: Header for the scene chunk when on file
//
struct SCENH
{
    int16_t bo;
    int16_t osk;
    int32_t nfrmLast;
    int32_t nfrmFirst;
    TRANS trans;
};

const auto kbomScenh = 0x5FC00000;
/** 3DMMv1.0: **************************************
    TAGC - Tag,Chid combo
****************************************/

/* 3DMMEx: On-disk representation of TAGC */
struct TAGCF
{
    CHID chid;
    TAGF tagf;
};
VERIFY_STRUCT_SIZE(TAGCF, 20)

const BOM kbomChid = 0xC0000000;
const BOM kbomTagc = kbomChid | (kbomTag >> 2);
typedef struct TAGC *PTAGC;
struct TAGC
{
    CHID chid;
    TAG tag;
};

/** 3DMMv1.0: **************************************
    SSE - scene sound event
****************************************/
const BOM kbomSse = 0xFF000000;
typedef struct SSE *PSSE;
struct SSE
{
    int32_t vlm;
    int32_t sty; // 3DMMv1.0: sound type
    bool fLoop;
    int32_t ctagc;
    // 3DMMv1.0:	TAGC _rgtagcSnd[_ctagc]; // variable array of tagcs follows SSE

  protected:
    static int32_t _Cb(int32_t ctagc)
    {
        return SIZEOF(SSE) + LwMul(ctagc, SIZEOF(TAGC));
    }
    SSE(void){};

  public:
    static PSSE PsseNew(int32_t ctagc);
    static PSSE PsseNew(int32_t vlm, int32_t sty, bool fLoop, int32_t ctagc, TAGC *prgtagc);
    static PSSE PsseDupFromGg(PGG pgg, int32_t iv, bool fDupTags = fTrue);

    PTAG Ptag(int32_t itagc)
    {
        PTAGC prgtagc = (PTAGC)PvAddBv(this, SIZEOF(SSE));
        return &(prgtagc[itagc].tag);
    }
    PTAGC Ptagc(int32_t itagc)
    {
        PTAGC prgtagc = (PTAGC)PvAddBv(this, SIZEOF(SSE));
        return &(prgtagc[itagc]);
    }
    CHID *Pchid(int32_t itagc)
    {
        PTAGC prgtagc = (PTAGC)PvAddBv(this, SIZEOF(SSE));
        return &(prgtagc[itagc].chid);
    }
    PSSE PsseAddTagChid(PTAG ptag, int32_t chid);
    PSSE PsseDup(void);
    void PlayAllSounds(PMVIE pmvie, uint32_t dtsStart = 0);
    void SwapBytes(void)
    {
        int32_t itagc;

        SwapBytesBom(this, kbomSse);
        for (itagc = 0; itagc < ctagc; itagc++)
        {
            SwapBytesBom(Ptag(itagc), kbomTag);
            SwapBytesBom(Pchid(itagc), kbomChid);
        }
    }
    int32_t Cb(void)
    {
        return SIZEOF(SSE) + LwMul(ctagc, SIZEOF(TAGC));
    }
};
void ReleasePpsse(PSSE *ppsse);

//
// 3DMMv1.0: Undo object for chopping operation.
//
typedef class SUNC *PSUNC;

#define SUNC_PAR MUNB
#define kclsSUNC KLCONST4('S', 'U', 'N', 'C')
class SUNC : public SUNC_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    CNO _cno;
    PCRF _pcrf;
    CTSTATE *_pctstate;
    STN _stnUndoName;
    SUNC(void)
    {
        _cno = cnoNil;
        _pcrf = pvNil;
        _pctstate = pvNil;
    }

  public:
    static PSUNC PsuncNew(void);
    ~SUNC(void);

    bool FSave(PSCEN pscen);
    void SetUndoName(const achar *pszUndoName)
    {
        _stnUndoName.SetSz(pszUndoName);
    }

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

//
// Lightweight undo for the two native frame-edge operations.  These paths
// change only the scene bounds (plus the persistent first-frame camera event),
// so serializing every actor and model would defeat the original instant frame
// creation behavior.
//
typedef class SUNF *PSUNF;

#define SUNF_PAR MUNB
#define kclsSUNF KLCONST4('S', 'U', 'N', 'F')
class SUNF : public SUNF_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    int32_t _cfrm;
    bool _fBefore;
    bool _fBlank;
    bool _fInserted;
    bool _fHasCameraState;
    CTSTATE *_pctstate;

    SUNF(void)
    {
        _cfrm = 0;
        _fBefore = fFalse;
        _fBlank = fFalse;
        _fInserted = fTrue;
        _fHasCameraState = fFalse;
        _pctstate = pvNil;
    }

  public:
    static PSUNF PsunfNew(void);
    ~SUNF(void);
    bool FSave(PSCEN pscen, bool fBefore, bool fBlank, int32_t cfrm);

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

//
// 3DMMv1.0: Undo object for background operations
//
typedef class SUNK *PSUNK;

#define SUNK_PAR MUNB
#define kclsSUNK KLCONST4('S', 'U', 'N', 'K')
class SUNK : public SUNK_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    TAG _tag;
    int32_t _icam;
    bool _fSetBkgd;
    SUNK(void)
    {
    }

  public:
    static PSUNK PsunkNew(void);
    ~SUNK(void);

    void SetTag(PTAG ptag)
    {
        _tag = *ptag;
    }
    void SetIcam(int32_t icam)
    {
        _icam = icam;
    }
    void SetFBkgd(bool fSetBkgd)
    {
        _fSetBkgd = fSetBkgd;
    }

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

//
// 3DMMv1.0: Undo object for transition operations
//
typedef class SUNR *PSUNR;

#define SUNR_PAR MUNB
#define kclsSUNR KLCONST4('S', 'U', 'N', 'R')
class SUNR : public SUNR_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    TRANS _trans;
    SUNR(void)
    {
    }

  public:
    static PSUNR PsunrNew(void);
    ~SUNR(void);

    void SetTrans(TRANS trans)
    {
        _trans = trans;
    }

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

//
// 3DMMv1.0: Undo object for pause operations
//
typedef class SUNP *PSUNP;

#define SUNP_PAR MUNB
#define kclsSUNP KLCONST4('S', 'U', 'N', 'P')
class SUNP : public SUNP_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    WIT _wit;
    int32_t _dts;
    bool _fAdd;
    SUNP(void)
    {
    }

  public:
    static PSUNP PsunpNew(void);
    ~SUNP(void);

    void SetWit(WIT wit)
    {
        _wit = wit;
    }
    void SetDts(int32_t dts)
    {
        _dts = dts;
    }
    void SetAdd(bool fAdd)
    {
        _fAdd = fAdd;
    }

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

//
// 3DMMv1.0: Undo object for text box operations
//
typedef class SUNX *PSUNX;

#define SUNX_PAR MUNB
#define kclsSUNX KLCONST4('S', 'U', 'N', 'X')
class SUNX : public SUNX_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    PTBOX _ptbox;
    bool _fAdd;
    int32_t _itbox;
    int32_t _nfrmFirst;
    int32_t _nfrmLast;
    SUNX(void)
    {
    }

  public:
    static PSUNX PsunxNew(void);
    ~SUNX(void);

    void SetNfrmFirst(int32_t nfrm)
    {
        _nfrmFirst = nfrm;
    }
    void SetNfrmLast(int32_t nfrm)
    {
        _nfrmLast = nfrm;
    }
    void SetItbox(int32_t itbox)
    {
        _itbox = itbox;
    }
    void SetTbox(PTBOX ptbox)
    {
        _ptbox = ptbox;
    }
    void SetAdd(bool fAdd)
    {
        _fAdd = fAdd;
    }

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

//
// 3DMMv1.0: Undo object for sound operations
//
typedef class SUNS *PSUNS;

#define SUNS_PAR MUNB
#define kclsSUNS KLCONST4('S', 'U', 'N', 'S')
class SUNS : public SUNS_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    PSSE _psse;   // 3DMMv1.0: may be pvNil
    int32_t _sty; // 3DMMv1.0: sty to use if _psse is pvNil

    SUNS(void)
    {
    }

  public:
    static PSUNS PsunsNew(void);
    ~SUNS(void);

    bool FSetSnd(PSSE psse)
    {
        PSSE psseDup = psse->PsseDup();
        if (psseDup == pvNil)
            return fFalse;
        ReleasePpsse(&_psse);
        _psse = psseDup;
        _sty = _psse->sty;
        return fTrue;
    }
    void SetSty(int32_t sty)
    {
        _sty = sty;
    }

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

//
// 3DMMv1.0: Undo object for title operations
//
typedef class SUNT *PSUNT;

#define SUNT_PAR MUNB
#define kclsSUNT KLCONST4('S', 'U', 'N', 'T')
class SUNT : public SUNT_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    STN _stn;
    SUNT(void)
    {
    }

  public:
    static PSUNT PsuntNew(void);
    ~SUNT(void);

    void SetName(PSTN pstn)
    {
        AssertPo(pstn, 0);
        _stn = *pstn;
    }

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

RTCLASS(SCEN)
RTCLASS(SUNT)
RTCLASS(SUNS)
RTCLASS(SUNA)
RTCLASS(SUNK)
RTCLASS(SUNP)
RTCLASS(SUNX)
RTCLASS(SUNC)
RTCLASS(SUNF)
RTCLASS(SUNR)

/** 3DMMv1.0: **************************************************
 *
 * Constructor for scenes.  This function is private, use PscenNew()
 * for public construction.
 *
 * Parameters:
 *  pmvie - The movie this scene belongs to.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
SCEN::SCEN(PMVIE pmvie)
{
    AssertNilOrPo(pmvie, 0);

    _pmvie = pmvie;
    _pactrSelected = pvNil;
    _pactrSelected2 = pvNil;
    _pglaridSelected = pvNil;
    _ptboxSelected = pvNil;
    _nfrmCur = 1;
    _nfrmLast = 1;
    _nfrmFirst = 1;
    _trans = transDissolve; // 3DMMv1.0: default transition

    //
    // 3DMMv1.0: By default we disable pauses in the studio
    //
    _grfscen = fscenPauses;
}

/** 3DMMv1.0: **************************************************
 *
 * Exported constructor for scenes.
 *
 * Parameters:
 *	pmvie - The movie this scene belongs to.
 *
 *
 * Returns:
 *  pvNil, on failure, else a pointer to an allocated SCEN object.
 *
 ****************************************************/
PSCEN SCEN::PscenNew(PMVIE pmvie)
{
    AssertNilOrPo(pmvie, 0);

    PSCEN pscen;

    //
    // 3DMMv1.0: Create the object
    //
    pscen = NewObj SCEN(pmvie);
    if (pscen == pvNil)
    {
        goto LFail;
    }

    //
    // 3DMMv1.0: Initialize event list
    //
    pscen->_pggsevFrm = GG::PggNew(SIZEOF(SEV));
    if (pscen->_pggsevFrm == pvNil)
    {
        goto LFail;
    }
    pscen->_isevFrmLim = 0;

    pscen->_pggsevStart = GG::PggNew(SIZEOF(SEV));
    if (pscen->_pggsevStart == pvNil)
    {
        goto LFail;
    }

    pscen->_pglpactr = GL::PglNew(SIZEOF(PACTR), 0);
    if (pscen->_pglpactr == pvNil)
    {
        goto LFail;
    }

    pscen->_pglaridSelected = GL::PglNew(SIZEOF(int32_t), 0);
    if (pscen->_pglaridSelected == pvNil)
    {
        goto LFail;
    }

    pscen->_pglptbox = GL::PglNew(SIZEOF(PTBOX), 0);
    if (pscen->_pglptbox == pvNil)
    {
        goto LFail;
    }

    if (vpcex != pvNil)
    {
        vpcex->EnqueueCid(cidSceneLoaded);
    }

    AssertPo(pscen, 0);
    return (pscen);

LFail:

    ReleasePpo(&pscen);
    return (pvNil);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for scenes.
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
SCEN::~SCEN(void)
{
    AssertBaseThis(0);

    int32_t isev;
    PSEV qsev;
    PTBOX ptbox;
    PACTR pactr;

    //
    // 3DMMv1.0: Remove starting events
    //

    if (_pggsevStart != pvNil)
    {
        for (isev = 0; isev < _pggsevStart->IvMac(); isev++)
        {
            qsev = (PSEV)_pggsevStart->QvFixedGet(isev);
            switch (qsev->sevt)
            {
            case sevtAddActr:
                _pggsevStart->Get(isev, &pactr);
                AssertPo(pactr, 0);
                ReleasePpo(&pactr);
                break;

            case sevtAddTbox:
                _pggsevStart->Get(isev, &ptbox);
                AssertPo(ptbox, 0);
                ReleasePpo(&ptbox);
                break;

            case sevtChngCamera:
            case sevtSetBkgd:
                break;

            case sevtPause:
            case sevtPlaySnd:
                Bug("Invalid event in event stream.");
                break;
            default:
                Bug("Unknown event type");
                break;
            }
        }
    }

    ReleasePpo(&_pggsevStart);

    //
    // 3DMMv1.0: Walk and delete all frame events.
    //
    if (_pggsevFrm != pvNil)
    {
        for (isev = 0; isev < _pggsevFrm->IvMac(); isev++)
        {

            //
            // 3DMMv1.0: For each event, release any child objects.
            //
            qsev = (PSEV)_pggsevFrm->QvFixedGet(isev);
            switch (qsev->sevt)
            {

            case sevtAddTbox:
            case sevtAddActr:
            case sevtSetBkgd:
                Assert(0, "Invalid event in event stream.");
                break;

            case sevtPlaySnd: {
                PSSE psse;
                int32_t itagc;
                psse = (PSSE)_pggsevFrm->QvGet(isev);
                for (itagc = 0; itagc < psse->ctagc; itagc++)
                {
                    TAGM::CloseTag(psse->Ptag(itagc));
                }
                break;
            }
            case sevtPause:
            case sevtChngCamera:
            case sevtBlankFrame:
                break;

            default:
                Bug("Unknown event type");
                break;
            }
        }
    }

    //
    // 3DMMv1.0: Delete frame event list.
    //
    ReleasePpo(&_pggsevFrm);

    //
    // 3DMMv1.0: Remove the GL of actors.  We do not Release the actors
    // 3DMMv1.0: themselves as our reference was released above in the
    // 3DMMv1.0: the _pggsevStart.
    //
    ReleasePpo(&_pglpactr);
    ReleasePpo(&_pglaridSelected);

    //
    // 3DMMv1.0: Remove the GL of tboxes.  We do not Release the tboxes
    // 3DMMv1.0: themselves as our reference was released above in the
    // 3DMMv1.0: the _pggsevStart.
    //
    ReleasePpo(&_pglptbox);

    //
    // 3DMMv1.0: Release the background
    //
    ReleasePpo(&_pbkgd);

    //
    // 3DMMv1.0: Release the thumbnail
    //
    ReleasePpo(&_pmbmp);

    //
    // 3DMMv1.0: Free the background sound
    //
    ReleasePpsse(&_psseBkgd);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for scenes.  This method is used to not only
 * destruct a scene, but to remove all lights, actors and
 * text boxes from the rendering area.  This is necessary
 * because Undo will hold references to actors, etc, causing
 * their destructors to not be called.
 *
 * Parameters:
 *  ppscen - A pointer to the scene to destroy.
 *
 * Returns:
 *  None.
 *
 ****************************************************/

void SCEN::Close(PSCEN *ppscen)
{
    AssertPo(*ppscen, 0);

    if (pvNil != (*ppscen)->_pbkgd)
    {
        (*ppscen)->_pbkgd->TurnOffLights();
    }
    (*ppscen)->HideActors();
    (*ppscen)->HideTboxes();
    ReleasePpo(ppscen);
}

#ifdef DEBUG

/** 3DMMv1.0: **************************************************
 * Mark memory used by the SCEN
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SCEN::MarkMem(void)
{
    AssertThis(0);

    int32_t iactr;
    int32_t itbox;
    PACTR pactr;
    PTBOX ptbox;

    SCEN_PAR::MarkMem();

    MarkMemObj(_pggsevStart);
    MarkMemObj(_pggsevFrm);
    MarkMemObj(_pglpactr);
    MarkMemObj(_pglaridSelected);
    MarkMemObj(_pglptbox);
    MarkMemObj(_pmbmp);

    for (iactr = 0; iactr < _pglpactr->IvMac(); iactr++)
    {
        _pglpactr->Get(iactr, &pactr);
        MarkMemObj(pactr);
    }

    for (itbox = 0; itbox < _pglptbox->IvMac(); itbox++)
    {
        _pglptbox->Get(itbox, &ptbox);
        MarkMemObj(ptbox);
    }

    if (_psseBkgd != pvNil)
        MarkPv(_psseBkgd);
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of the SCEN.
***************************************************************************/
void SCEN::AssertValid(uint32_t grf)
{
    int32_t isev;
    SEV sev;

    SCEN_PAR::AssertValid(fobjAllocated);

    AssertPo(&_stnName, 0);
    AssertNilOrPo(_pactrSelected, 0);
    AssertNilOrPo(_pactrSelected2, 0);
    AssertNilOrPo(_pbkgd, 0);
    AssertNilOrPo(_pmbmp, 0);
    AssertPo(_pglpactr, 0);
    AssertPo(_pglaridSelected, 0);
    AssertPo(_pglptbox, 0);
    AssertPo(_pggsevFrm, 0);
    AssertPo(_pggsevStart, 0);
    AssertPo(_pmvie, 0);

    for (isev = 0; isev < _pggsevFrm->IvMac(); isev++)
    {
        sev = *(PSEV)_pggsevFrm->QvFixedGet(isev);
        switch (sev.sevt)
        {
        case sevtPlaySnd:
        case sevtPause:
        case sevtChngCamera:
        case sevtBlankFrame:
            break;
        default:
            Bug("Unknown event type");
        }
    }

    for (isev = 0; isev < _pggsevStart->IvMac(); isev++)
    {
        sev = *(PSEV)_pggsevStart->QvFixedGet(isev);
        switch (sev.sevt)
        {
        case sevtAddActr:
        case sevtSetBkgd:
        case sevtAddTbox:
            break;
        default:
            Bug("Unknown event type");
        }
    }
}

#endif

/** 3DMMv1.0: **************************************************
 *
 * Sets the transition of the scene and creates an undo object.
 *
 * Parameters:
 *	trans - Transition type.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FSetTransition(TRANS trans)
{
    AssertThis(0);

    PSUNR psunr;

    psunr = SUNR::PsunrNew();

    if (psunr == pvNil)
    {
        return (fFalse);
    }

    if (!_pmvie->FAddUndo(psunr))
    {
        return (fFalse);
    }

    ReleasePpo(&psunr);

    SetTransitionCore(trans);

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * Sets the name of the scene and creates an undo object.
 *
 * Parameters:
 *	psz - Null terminated string.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FSetName(PSTN pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    PSUNT psunt;

    psunt = SUNT::PsuntNew();

    if (psunt != pvNil)
    {
        psunt->SetName(&_stnName);
    }

    if (psunt == pvNil)
    {
        return (fFalse);
    }

    if (!_pmvie->FAddUndo(psunt))
    {
        return (fFalse);
    }

    ReleasePpo(&psunt);

    SetNameCore(pstn);

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine jumps to an arbitrary frame, updating actors, and adding
 * new frames to the scene if needed.
 *
 * Parameters:
 *	nFrm - The frame number to jump to.  This may be positive or negative
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/

bool SCEN::FGotoFrm(int32_t nfrm)
{
    AssertThis(0);

    MVIE::DiagLog("SCEN::FGotoFrm enter scene=%ld old=%ld target=%ld first=%ld last=%ld event_cursor=%ld events=%ld actors=%ld tboxes=%ld grf=0x%08lX",
                  (long)Pmvie()->Iscen(), (long)_nfrmCur, (long)nfrm,
                  (long)_nfrmFirst, (long)_nfrmLast, (long)_isevFrmLim,
                  (long)_pggsevFrm->IvMac(), (long)_pglpactr->IvMac(),
                  (long)_pglptbox->IvMac(), (unsigned long)_grfscen);
    MVIE::DiagSetContext(Pmvie(), "SCEN::FGotoFrm enter");

    bool fSoundInFrame = fFalse, fUpdateSndFrame = nfrm != _nfrmCur;
    SEV sev;
    PMVU pmvu;
    void *qvVar;
    int32_t isev;
    int32_t nfrmOld = _nfrmCur;
    uint64_t qwPerfStage = 0;
    uint32_t cusecPerfForceActors = 0;
    uint32_t cusecPerfForceTboxes = 0;
    uint32_t cusecPerfVisibility = 0;
    uint32_t cusecPerfCamera = 0;
    uint32_t cusecPerfPrerender = 0;

    if (nfrm < _nfrmCur)
    {

        //
        // 3DMMv1.0: Assume no pause type
        //
        Pmvie()->Pmcc()->PauseType(witNil);

        //
        // 3DMMv1.0: Go backwards
        //
        if (nfrm < _nfrmFirst)
        {
            //
            // 3DMMv1.0: Move first frame back in time
            //
            _MoveBackFirstFrame(nfrm);
            _MarkMovieDirty();
        }

        _nfrmCur = nfrm;

        //
        // 3DMMv1.0: Unplay all events to dest frame.
        //
        for (; _isevFrmLim > 0; _isevFrmLim--)
        {
            _pggsevFrm->GetFixed(_isevFrmLim - 1, &sev);
            if (sev.nfrm <= _nfrmCur)
            {
                break;
            }

            qvVar = _pggsevFrm->QvGet(_isevFrmLim - 1);
            MVIE::DiagLog("SCEN::FGotoFrm unplay event index=%ld event_frame=%ld type=%ld",
                          (long)(_isevFrmLim - 1), (long)sev.nfrm, (long)sev.sevt);
            if (!_FUnPlaySev(&sev, qvVar))
            {
                PushErc(ercSocGotoFrameFailure);
                return (fFalse);
            }
        }

        //
        // 3DMMv1.0: Play events in this frame
        //
        for (isev = _isevFrmLim - 1; isev >= 0; isev--)
        {
            _pggsevFrm->GetFixed(isev, &sev);
            if (sev.nfrm < _nfrmCur)
            {
                break;
            }
            qvVar = _pggsevFrm->QvGet(isev);
            MVIE::DiagLog("SCEN::FGotoFrm replay event index=%ld event_frame=%ld type=%ld",
                          (long)isev, (long)sev.nfrm, (long)sev.sevt);
            if (!_FPlaySev(&sev, qvVar, _grfscen))
            {
                PushErc(ercSocGotoFrameFailure);
                return (fFalse);
            }
            if (sev.sevt == sevtPlaySnd && sev.nfrm == _nfrmCur)
                fSoundInFrame = fTrue;
        }
    }
    else if (nfrm > _nfrmCur)
    {

        pmvu = (PMVU)Pmvie()->PddgGet(0);
        AssertNilOrPo(pmvu, 0);

        if ((pmvu != pvNil) && (pmvu->Tool() == toolRecordSameAction))
        {
            Pmvie()->Pmcc()->PlayUISound(toolRecordSameAction);
        }

        _nfrmCur = nfrm;

        //
        // 3DMMv1.0: Assume no pause type
        //
        Pmvie()->Pmcc()->PauseType(witNil);

        //
        // 3DMMv1.0: Go forwards
        //
        if (_nfrmCur > _nfrmLast)
        {
            _nfrmLast = _nfrmCur;
            _MarkMovieDirty();
        }

        //
        // 3DMMv1.0: Play all events to dest frame.
        //
        for (; _isevFrmLim < _pggsevFrm->IvMac(); _isevFrmLim++)
        {
            _pggsevFrm->GetFixed(_isevFrmLim, &sev);
            if (sev.nfrm > _nfrmCur)
            {
                break;
            }
            qvVar = _pggsevFrm->QvGet(_isevFrmLim);
            MVIE::DiagLog("SCEN::FGotoFrm play event index=%ld event_frame=%ld type=%ld current=%ld",
                          (long)_isevFrmLim, (long)sev.nfrm, (long)sev.sevt, (long)_nfrmCur);
            if (!_FPlaySev(&sev, qvVar, (sev.nfrm == _nfrmCur ? _grfscen : (_grfscen | fscenSounds | fscenPauses))))
            {
                PushErc(ercSocGotoFrameFailure);
                return (fFalse);
            }
            if (sev.sevt == sevtPlaySnd && sev.nfrm == _nfrmCur)
                fSoundInFrame = fTrue;
        }
    }

    MVIE::DiagSetContext(Pmvie(), "SCEN::FGotoFrm before _FForceActorsToFrm");
    if (MVIE::FPerformanceMode())
        qwPerfStage = MVIE::PerfNow();
    if (!(_grfscen & fscenActrs) && !_FForceActorsToFrm(nfrm, &fSoundInFrame))
    {
        return (fFalse);
    }

    if (MVIE::FPerformanceMode())
        cusecPerfForceActors = MVIE::PerfElapsedUs(qwPerfStage);
    MVIE::DiagSetContext(Pmvie(), "SCEN::FGotoFrm after _FForceActorsToFrm/before _FForceTboxesToFrm");
    if (MVIE::FPerformanceMode())
        qwPerfStage = MVIE::PerfNow();
    if (!(_grfscen & fscenTboxes) && !_FForceTboxesToFrm(nfrm))
    {
        return (fFalse);
    }

    if (MVIE::FPerformanceMode())
        cusecPerfForceTboxes = MVIE::PerfElapsedUs(qwPerfStage);
    MVIE::DiagSetContext(Pmvie(), "SCEN::FGotoFrm after _FForceTboxesToFrm");

    if (MVIE::FPerformanceMode())
        qwPerfStage = MVIE::PerfNow();
    // A Shift+Ctrl-inserted blank frame owns no actor/text visibility, but it
    // does not alter any actor route or text-box lifetime.  Reapply ordinary
    // visibility on every nonblank frame so leaving a blank frame is clean.
    bool fBlankFrame = fFalse;
    for (isev = 0; isev < _pggsevFrm->IvMac(); isev++)
    {
        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.nfrm > _nfrmCur)
            break;
        if (sev.nfrm == _nfrmCur && sev.sevt == sevtBlankFrame)
        {
            fBlankFrame = fTrue;
            break;
        }
    }

    // BODY visibility is reference-counted.  The prerender subsystem owns one
    // of those hide/show references while an actor is baked into the current
    // background.  The blank-frame visibility pass must not consume that
    // reference or a later prerender Show() will drive _cactHidden below zero.
    if (fBlankFrame)
    {
        bool fAnyPrerendered = fFalse;
        for (int32_t iactr = 0; iactr < _pglpactr->IvMac(); iactr++)
        {
            PACTR pactr;
            _pglpactr->Get(iactr, &pactr);
            if (pactr->FPrerendered())
            {
                fAnyPrerendered = fTrue;
                break;
            }
        }
        if (fAnyPrerendered)
            _EndPrerendering();
    }

    for (int32_t iactr = 0; iactr < _pglpactr->IvMac(); iactr++)
    {
        PACTR pactr;
        _pglpactr->Get(iactr, &pactr);
        bool fShouldBeVisible = !fBlankFrame && pactr->FOnStage();
        bool fVisible = pactr->Pbody()->FVisible();
        if (fShouldBeVisible && !fVisible && !pactr->FPrerendered())
            pactr->Show();
        else if (!fShouldBeVisible && fVisible)
            pactr->Hide();
    }
    if (fBlankFrame)
    {
        SelectActr(pvNil);
        SelectTbox(pvNil);
        for (int32_t itbox = 0; itbox < _pglptbox->IvMac(); itbox++)
        {
            PTBOX ptbox;
            _pglptbox->Get(itbox, &ptbox);
            ptbox->FGotoFrame(ptbox->NfrmFirst() - 1);
        }
    }

    if (MVIE::FPerformanceMode())
        cusecPerfVisibility = MVIE::PerfElapsedUs(qwPerfStage);

    // Prerendering snapshots the BRender world from the camera that is active
    // at the instant BWLD::Prerender() runs.  Install the .3ct-adjusted camera
    // first so a static held Manual/Depth camera can use the stock optimization
    // without baking actors from the scene's baseline viewpoint.
    MVIE::DiagSetContext(Pmvie(), "SCEN::FGotoFrm before ApplyCameraTrack/prerender");
    if (MVIE::FPerformanceMode())
        qwPerfStage = MVIE::PerfNow();
    Pmvie()->ApplyCameraTrack();
    if (MVIE::FPerformanceMode())
        cusecPerfCamera += MVIE::PerfElapsedUs(qwPerfStage);

    if (!(_grfscen & fscenActrs) && !fBlankFrame)
    {
        MVIE::DiagSetContext(Pmvie(), "SCEN::FGotoFrm before _DoPrerenderingWork");
        if (MVIE::FPerformanceMode())
            qwPerfStage = MVIE::PerfNow();
        _DoPrerenderingWork(fFalse);
        if (MVIE::FPerformanceMode())
            cusecPerfPrerender += MVIE::PerfElapsedUs(qwPerfStage);
        MVIE::DiagSetContext(Pmvie(), "SCEN::FGotoFrm after _DoPrerenderingWork");
    }

    // Frame-slider jumps and other editor seeks must preview the same camera
    // position that playback will render.  FGotoFrm has now finished replaying
    // the scene's ordinary camera events, so the scene-local .3ct offset gets
    // the final word before the view is invalidated and redrawn.
    if (MVIE::FPerformanceMode())
        qwPerfStage = MVIE::PerfNow();
    Pmvie()->ApplyCameraTrack();
    if (MVIE::FPerformanceMode())
        cusecPerfCamera += MVIE::PerfElapsedUs(qwPerfStage);

    // Playback refreshes attached lights in MVIE::FCmdRender. Editor seeks do
    // not, so centralize the non-playing refresh here after every actor event
    // and camera sample for the destination frame has been applied.
    if (!Pmvie()->FPlaying())
    {
        Pmvie()->UpdateTestLightAttachment();
        MVIE::LightEditorLog(Pmvie(), "editor_goto_frame complete target=%ld", (long)_nfrmCur);
    }
    Pmvie()->InvalViews();
    MVIE::DiagSetContext(Pmvie(), "SCEN::FGotoFrm complete");

    if (MVIE::FPerformanceMode())
        MVIE::PerfAddSceneTimings(cusecPerfForceActors, cusecPerfForceTboxes,
                                  cusecPerfVisibility, cusecPerfCamera, cusecPerfPrerender);

    if (fUpdateSndFrame)
        Pmvie()->Pmcc()->SetSndFrame(fSoundInFrame);

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This function is an optimization which could be completely
 * skipped if desired.  The idea is to not render actors frame
 * after frame if they're not moving or changing at all.  So
 * when we hit a camera change, we look ahead to see if any
 * actors are unchanging before the next camera change.  If
 * there are, we hide the changing actors (so only the unchanging
 * ones are visible), then "take a snapshot" of the world with
 * the unchanging actors (via BWLD::Prerender()), and use the
 * snapshot as the background RGB and Z buffer until the next
 * camera view change.  We only prerender if the movie is
 * playing.
 *
 * Parameters:
 *	fStartNow - if fTrue, start prerendering even if there's no
 *              camera change in this frame.  If fFalse, only
 *              start prerendering if there is a camera view
 *              change in this frame, or if this is the first
 *              frame of the scene.
 *
 * Returns:
 *  none
 *
 ****************************************************/
void SCEN::_DoPrerenderingWork(bool fStartNow)
{
    AssertThis(0);

#if defined(BRENDER_MODERN_14)
    // The legacy optimization bakes static actors into BWLD's CPU RGB/Z
    // background buffers, then hides those actors for subsequent playback
    // frames. Modern BRender/OpenGL renders the authoritative actor image on
    // the GPU, so that 1995 CPU/GDI snapshot does not contain the completed
    // modern frame. Hiding the supposedly baked actors therefore makes static
    // objects black/invisible while the movie plays. Keep actors live in the
    // Modern renderer; the legacy renderer retains its original optimization.
    return;
#endif

    int32_t isev;
    SEV sev;
    int32_t nfrmNextChange;
    int32_t ipactr;
    PACTR pactr;
    int32_t cactrPrerendered;

    // A prerendered actor has already been baked into the background buffers.
    // The old camera-track guard disabled prerendering for an entire scene if
    // that scene contained even one .3ct key, which makes large static portions
    // unnecessarily expensive.  End a snapshot only while the camera is truly
    // changing around this frame.  Static held .3ct poses are allowed to use
    // normal 3DMM prerendering from the adjusted camera installed by FGotoFrm.
    bool fAnyPrerendered = fFalse;
    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        if (pactr->FPrerendered())
        {
            fAnyPrerendered = fTrue;
            break;
        }
    }

    if (MVIE::FSceneDynamicLightingActive() || Pmvie()->FCameraTrackNeedsLiveActors())
    {
        if (fAnyPrerendered)
            _EndPrerendering();
        return;
    }

    // If a moving custom camera has just settled into a held pose, there is no
    // native sevtChngCamera event to restart the optimization.  Start one fresh
    // snapshot the first time we reach that stable section.
    if (Pmvie()->FCameraTrackActive() && !fAnyPrerendered)
        fStartNow = fTrue;

    //
    // 3DMMv1.0: If the movie was playing and there was a camera view change
    // 3DMMv1.0: in this frame, prerender any actors that don't change from
    // 3DMMv1.0: here to the next camera view change or the end of the scene.
    //
    if (!Pmvie()->FPlaying())
    {
        return; // 3DMMv1.0: only prerender if the movie is playing
    }

    // 3DMMv1.0: Do prerender if this is the first frame of the scene
    // 3DMMv1.0: (even though there's no sevtChngCamera), or if fStartNow
    // 3DMMv1.0: is fTrue, or if there is a sevtChngCamera in this
    // 3DMMv1.0: frame.  Otherwise, just return.
    if (_nfrmCur != _nfrmFirst && !fStartNow)
    {
        for (isev = _isevFrmLim - 1; isev >= 0; isev--)
        {
            _pggsevFrm->GetFixed(isev, &sev);
            if (sev.nfrm != _nfrmCur)
            {
                return; // 3DMMv1.0: no camera view change in this frame
            }
            if (sev.sevt == sevtChngCamera)
            {
                break; // 3DMMv1.0: found one!
            }
        }
        if (isev < 0)
        {
            return; // 3DMMv1.0: no camera view in this frame
        }
    }

    // 3DMMv1.0: Find when the next view change is
    nfrmNextChange = _nfrmLast; // 3DMMv1.0: if no more view changes, go til end of scene
    for (isev = _isevFrmLim; isev < _pggsevFrm->IvMac(); isev++)
    {
        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.sevt == sevtChngCamera)
        {
            nfrmNextChange = sev.nfrm;
            break;
        }
    }

    // 3DMMv1.0: Hide all actors that can't be prerendered this time, and count how many
    // 3DMMv1.0: can be prerendered this time
    cactrPrerendered = 0;
    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        if (pactr->FMustRender(nfrmNextChange))
        {
            pactr->Hide();
        }
        else
        {
            cactrPrerendered++;
            if (pactr->FPrerendered())
            {
                // 3DMMv1.0: Actor was prerendered in last view and in this
                // 3DMMv1.0: view.  Temporarily show the actor so it shows
                // 3DMMv1.0: up in the prerendered background
                pactr->Show();
                pactr->SetPrerendered(fFalse);
            }
        }
    }
    if (cactrPrerendered > 0)
    {
        Pmvie()->Pbwld()->Prerender();
    }
    // 3DMMv1.0: Show all the actors that were hidden (and show them again if they were
    // 3DMMv1.0: prerendered last time and can't be now), and hide the newly prerendered
    // 3DMMv1.0: actors.
    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        if (pactr->FMustRender(nfrmNextChange))
        {
            pactr->Show();
            if (pactr->FPrerendered())
            {
                pactr->Show();
                pactr->SetPrerendered(fFalse);
            }
        }
        else
        {
            Assert(!pactr->FPrerendered(), "no actor should be marked prerendered here");
            pactr->Hide();
            pactr->SetPrerendered(fTrue);
        }
    }
}

/** 3DMMv1.0: **************************************************
 *
 * 	Ends any current prerendering by restoring the background
 *  RGB and Z buffers of the BWLD, showing all previously
 *  hidden actors, and marking all actors as not prerendered.
 *
 * Parameters:
 *	none
 *
 * Returns:
 *  none
 *
 ****************************************************/
void SCEN::_EndPrerendering(void)
{
    AssertThis(0);

    int32_t ipactr;
    PACTR pactr;

    // 3DMMv1.0: Show all the actors that were being prerendered
    Pmvie()->Pbwld()->Unprerender();
    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        if (pactr->FPrerendered())
        {
            pactr->Show();
            pactr->SetPrerendered(fFalse);
        }
    }
}

/** 3DMMv1.0: **************************************************
 *
 * This routine replays all the events, filtered by
 * grfscen, in the current frame.
 *
 * Parameters:
 *	grfscen - Events to play.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FReplayFrm(uint32_t grfscen)
{
    AssertThis(0);

    SEV sev;
    void *qvVar;
    int32_t isev, iactr;
    int32_t nfrmOld = _nfrmCur;
    PACTR pactr;

    //
    // 3DMMv1.0: Play events in this frame
    //
    for (isev = _isevFrmLim - 1; isev >= 0; isev--)
    {
        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.nfrm < _nfrmCur)
        {
            break;
        }
        qvVar = _pggsevFrm->QvGet(isev);
        // 3DMMv1.0: Note: FReplayFrm always suppresses camera changes
        if (!_FPlaySev(&sev, qvVar, (~grfscen) | fscenCams))
        {
            PushErc(ercSocGotoFrameFailure);
            return (fFalse);
        }
    }

    if (grfscen & fscenActrs)
    {

        for (iactr = 0; iactr < _pglpactr->IvMac(); iactr++)
        {
            _pglpactr->Get(iactr, &pactr);
            AssertPo(pactr, 0);

            if (!pactr->FReplayFrame(grfscen))
            {
                return (fFalse);
            }
        }
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine recalculates the length of the movie.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *	None.
 *
 ****************************************************/
void SCEN::InvalFrmRange(void)
{
    AssertThis(0);

    PACTR pactr;
    PTBOX ptbox;
    int32_t ipo;
    int32_t nfrmStart, nfrmLast;

    for (ipo = 0; ipo < _pglpactr->IvMac(); ipo++)
    {
        _pglpactr->Get(ipo, &pactr);

        if (pactr->FGetLifetime(&nfrmStart, &nfrmLast))
        {

            if (nfrmStart < _nfrmFirst)
            {
                _MoveBackFirstFrame(nfrmStart);
            }

            if (nfrmLast > _nfrmLast)
            {
                _nfrmLast = nfrmLast;
            }
        }
    }

    for (ipo = 0; ipo < _pglptbox->IvMac(); ipo++)
    {
        _pglptbox->Get(ipo, &ptbox);

        if (ptbox->FGetLifetime(&nfrmStart, &nfrmLast))
        {

            if (nfrmStart < _nfrmFirst)
            {
                _MoveBackFirstFrame(nfrmStart);
            }

            if (nfrmLast > _nfrmLast)
            {
                _nfrmLast = nfrmLast;
            }
        }
    }
}

/** 3DMMv1.0: **************************************************
 *
 * This routine plays a single event.
 *
 * Be careful not to modify _pggsevFrm or _pggsevStart from
 * within this routine.
 *
 * Parameters:
 *	psev - Pointer to the scene event to play.
 *	qvVar- Pointer to the variable part of the event.
 *	grfscen - Flags of currently disabled event types.
 *
 * Returns:
 *	fTrue if the event was played, fFalse in the case of failure.
 *
 ****************************************************/
bool SCEN::_FPlaySev(PSEV psev, void *qvVar, uint32_t grfscen)
{
    AssertThis(0);
    AssertVarMem(psev);

    PACTR pactr;
    PTBOX ptbox;
    PBKGD pbkgd;
    TAG tag;
    WIT wit;
    int32_t dts;

    switch (psev->sevt)
    {
    case sevtPlaySnd:
        PSSE psse;

        psse = (PSSE)qvVar;
        // 3DMMv1.0: If it's midi, copy it to _psseBkgd
        if (psse->sty == styMidi)
        {
            PSSE psseDup;
            psseDup = psse->PsseDup();
            if (psseDup == pvNil)
                return fFalse;
            ReleasePpsse(&_psseBkgd);
            _psseBkgd = psseDup;
            _nfrmSseBkgd = psev->nfrm;
        }
        if (grfscen & fscenSounds)
        {
            return (fTrue);
        }
        psse->PlayAllSounds(Pmvie());
        break;

    case sevtSetBkgd:

        tag = *(PTAG)qvVar;
        pbkgd = (PBKGD)vptagm->PbacoFetch(&tag, BKGD::FReadBkgd);
        if (pvNil == pbkgd)
        {
            return fFalse;
        }

        if (!pbkgd->FSetCamera(_pmvie->Pbwld(), 0))
        {
            ReleasePpo(&pbkgd);
            return (fFalse);
        }

        if (Pmvie()->Trans() == transNil)
        {
            Pmvie()->SetTrans(transCut); // 3DMMv1.0: so we do palette change at next draw
        }

        _pbkgd = pbkgd;
        _tagBkgd = tag;
        break;

    case sevtAddActr:

        //
        // 3DMMv1.0: Add the actor to the roll call.
        //
        pactr = *(PACTR *)qvVar;

        AssertPo(pactr, 0);
        MODERN_BR_SCENE_LOG("SCEN::_FPlaySev AddActr BEGIN scene=%p actor=%p arid=%ld body=%p frame=%ld scene_roll_before=%ld",
                            this, pactr, (long)pactr->Arid(), pactr->Pbody(), (long)_nfrmCur,
                            (long)_pglpactr->IvMac());

        if (_pglpactr->FPush(&pactr) == fFalse)
        {
            MODERN_BR_SCENE_LOG("SCEN::_FPlaySev AddActr FAIL FPush actor=%p arid=%ld",
                                pactr, (long)pactr->Arid());
            return (fFalse);
        }

        pactr->SetPscen(this);
        if (pactr->Pbody() == pvNil)
        {
            TAG tagTmpl;
            pactr->GetTagTmpl(&tagTmpl);
            MODERN_BR_SCENE_LOG("SCEN::_FPlaySev AddActr FAIL create_body actor=%p arid=%ld sid=%ld ctg=0x%08lX cno=%ld",
                                pactr, (long)pactr->Arid(), (long)tagTmpl.sid,
                                (unsigned long)tagTmpl.ctg, (long)tagTmpl.cno);
            MVIE::MultiLog(_pmvie,
                "actor_insert FAIL stage=create_body arid=%ld sid=%ld ctg=0x%08lX cno=%ld",
                (long)pactr->Arid(), (long)tagTmpl.sid,
                (unsigned long)tagTmpl.ctg, (long)tagTmpl.cno);
            _pglpactr->FPop(&pactr);
            return fFalse;
        }

        //
        // 3DMMv1.0: Plop them into this frame
        //
        if (!pactr->FGotoFrame(_nfrmCur))
        {
            MODERN_BR_SCENE_LOG("SCEN::_FPlaySev AddActr FAIL FGotoFrame actor=%p arid=%ld frame=%ld",
                                pactr, (long)pactr->Arid(), (long)_nfrmCur);
            _pglpactr->FPop(&pactr);
            return (fFalse);
        }

        MODERN_BR_SCENE_LOG("SCEN::_FPlaySev AddActr OK actor=%p arid=%ld body=%p onstage=%d scene_roll_after=%ld",
                            pactr, (long)pactr->Arid(), pactr->Pbody(), (int)pactr->FOnStage(),
                            (long)_pglpactr->IvMac());
        break;

    case sevtPause:

        wit = (WIT)(*(int32_t *)qvVar);
        Pmvie()->Pmcc()->PauseType(wit);

        if (grfscen & fscenPauses)
        {
            return (fTrue);
        }

        dts = *((int32_t *)qvVar + 1);
        Pmvie()->DoPause(wit, dts);
        break;

    case sevtAddTbox:

        //
        // 3DMMv1.0: Add the text box to the scene
        //
        ptbox = *(PTBOX *)qvVar;

        AssertPo(ptbox, 0);

        //
        // 3DMMv1.0: Insert at the end, because tbox ordering is important
        // 3DMMv1.0: since the client uses itbox to find text boxes.
        //
        if (!_pglptbox->FInsert(_pglptbox->IvMac(), &ptbox))
        {
            return (fFalse);
        }

        //
        // 3DMMv1.0: Plop them into this frame
        //
        if (!ptbox->FGotoFrame(_nfrmCur))
        {
            _pglptbox->Delete(_pglptbox->IvMac() - 1);
            return (fFalse);
        }

        break;

    case sevtChngCamera:

        Assert(_pbkgd != pvNil, "No background in the scene");
        if (grfscen & fscenCams)
        {
            return (fTrue);
        }
        if (!_pbkgd->FSetCamera(_pmvie->Pbwld(), *(int32_t *)qvVar))
        {
            return (fFalse);
        }
        break;

    case sevtBlankFrame:
        // Visibility is applied after actors and text boxes reach the frame.
        break;

    default:

        Bug("Unhandled sevt");
        break;
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine undoes a single event.
 *
 * Parameters:
 *	psev - Pointer to the scene event to unplay.
 *	qvVar- Pointer to the variable part of the event.
 *
 *
 * Returns:
 *	fTrue if the unplay worked, else fFalse.
 *
 ****************************************************/
bool SCEN::_FUnPlaySev(PSEV psev, void *qvVar)
{
    AssertThis(0);
    AssertVarMem(psev);

    SEV sev;
    int32_t isev;
    PSEV qsevTmp;

    switch (psev->sevt)
    {
    case sevtPlaySnd:
        //
        // 3DMMv1.0: Go backwards and find previous background sound.
        //
        for (isev = _isevFrmLim - 1; isev >= 0; isev--)
        {
            qsevTmp = (PSEV)_pggsevFrm->QvFixedGet(isev);

            if ((qsevTmp->sevt == sevtPlaySnd) && (qsevTmp->nfrm < _nfrmCur))
            {
                sev = *qsevTmp;
                _FPlaySev(&sev, _pggsevFrm->QvGet(isev), _grfscen | fscenSounds); // 3DMMv1.0: Ignore failure.
                return (fTrue);
            }
        }

        //
        // 3DMMv1.0: Not found -- clear variables.
        //
        ReleasePpsse(&_psseBkgd);
        break;

    case sevtAddActr:
    case sevtAddTbox:
    case sevtPause:
    case sevtBlankFrame:
        break;

    case sevtChngCamera:

        Assert(_pbkgd != pvNil, "No background in scene");

        //
        // 3DMMv1.0: Go backwards and find previous camera position.
        //
        for (isev = _isevFrmLim - 1; isev >= 0; isev--)
        {
            qsevTmp = (PSEV)_pggsevFrm->QvFixedGet(isev);

            if ((qsevTmp->sevt == sevtChngCamera) && (qsevTmp->nfrm < _nfrmCur))
            {
                sev = *qsevTmp;
                AssertDo(_FPlaySev(&sev, _pggsevFrm->QvGet(isev), _grfscen), "Should not fail.");
                return (fTrue);
            }
        }

        //
        // 3DMMv1.0: Not found -- use starting camera, which is always camera 0.
        //
        sev.sevt = sevtChngCamera;
        isev = 0;

        if (!_FPlaySev(&sev, &isev, _grfscen))
        {
            Assert(0, "Should never happen");
            return (fFalse);
        }

        break;
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine moves back stuff that is currently
 * in the first frame of the movie to the given frame.
 *
 * Parameters:
 *	nfrm - New first frame.
 *
 * Returns:
 *	None.
 *
 ****************************************************/
void SCEN::PreserveFirstFrameBoundary(int32_t nfrmFirst)
{
    AssertThis(0);
    if (nfrmFirst < _nfrmFirst)
        _MoveBackFirstFrame(nfrmFirst);
}

/***************************************************************************
    PreserveFirstFrameBoundary intentionally funnels through the native
    first-frame extension path so persistent first-frame camera events move
    with the restored scene boundary.
***************************************************************************/
void SCEN::_MoveBackFirstFrame(int32_t nfrm)
{
    AssertThis(0);
    Assert(nfrm < _nfrmFirst, "Can only be called to extend scene back.");

    int32_t isev;
    SEV sev;

    //
    // 3DMMv1.0: Move back all events that must persist in the
    // 3DMMv1.0: first frame.
    //
    for (isev = 0; isev < _pggsevFrm->IvMac(); isev++)
    {
        _pggsevFrm->GetFixed(isev, &sev);

        if (sev.nfrm != _nfrmFirst)
        {
            break;
        }

        if (sev.sevt == sevtChngCamera)
        {

            //
            // 3DMMv1.0: Move this back
            //
            sev.nfrm = nfrm;
            _pggsevFrm->PutFixed(isev, &sev);
            _pggsevFrm->Move(isev, 0);
        }
    }

    //
    // 3DMMv1.0: Set new first frame
    //
    _nfrmFirst = nfrm;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds a sound to the event list for this frame.
 *
 * Parameters:
 *  fLoop - should the sound loop?
 *  fQueue - queue after existing sounds, or replace them?
 *  vlm - volume to play this sound at
 *  sty - sound type (midi, speech, or SFX)
 *  ctag - number of sounds
 *  prgtag - array of MSND tags
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FAddSndCore(bool fLoop, bool fQueue, int32_t vlm, int32_t sty, int32_t ctag, PTAG prgtag)
{
    AssertThis(0);
    AssertPvCb(prgtag, LwMul(ctag, SIZEOF(TAG)));
    Assert(!fQueue || (ctag == 1), "if fQueue'ing, you should only be adding one sound");
    Assert(!fQueue || !fLoop, "can't both queue and loop");
    AssertIn(sty, 0, styLim);

    SEV sev;
    PSSE psseOld;
    PSSE psseNew;
    int32_t isev;
    CHID chid;
    int32_t isevSnd = ivNil;
    PTAG ptag;
    PMSND pmsnd;
    int32_t itag, itagBase;

    //
    // 3DMMv1.0: Find any other sevtPlaySnd events in this frame with the same sty
    //
    for (isev = _isevFrmLim - 1; isev >= 0; isev--)
    {
        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.nfrm != _nfrmCur)
            break;
        if (sev.sevt == sevtPlaySnd)
        {
            psseOld = (PSSE)_pggsevFrm->QvGet(isev);
            if (psseOld->sty == sty)
            {
                // 3DMMv1.0: Found a match, which we will either add to or replace
                isevSnd = isev;
                break;
            }
        }
    }

    if (isevSnd == ivNil)
    {
        fQueue = fFalse; // 3DMMv1.0: nothing to queue to
    }

    sev.nfrm = _nfrmCur;
    sev.sevt = sevtPlaySnd;

    if (!fQueue)
    {
        PTAGC prgtagc;
        int32_t itagc;

        if (!FAllocPv((void **)&prgtagc, LwMul(SIZEOF(TAGC), ctag), fmemClear, mprNormal))
            return fFalse;
        for (itagc = 0; itagc < ctag; itagc++)
        {
            prgtagc[itagc].tag = prgtag[itagc];
            if (prgtagc[itagc].tag.sid == ksidUseCrf)
            {
                if (!_pmvie->FChidFromUserSndCno(prgtag[itagc].cno, &chid))
                {
                    FreePpv((void **)&prgtagc);
                    return fFalse;
                }
                prgtagc[itagc].chid = chid;
            }
            else
            {
                TrashVar(&prgtagc[itagc].chid);
            }
        }
        // 3DMMv1.0: Create new event, replace any old event of same sty
        itagBase = 0;
        psseNew = SSE::PsseNew(vlm, sty, fLoop, ctag, prgtagc);
        FreePpv((void **)&prgtagc);
        if (pvNil == psseNew)
        {
            return fFalse;
        }
        if (!_FAddSev(&sev, psseNew->Cb(), psseNew))
        {
            ReleasePpsse(&psseNew);
            return fFalse;
        }
        if (isevSnd != ivNil)
        {
            // 3DMMv1.0: Delete old event, if any
            PSSE psse;
            int32_t itagc;
            psse = (PSSE)_pggsevFrm->QvGet(isevSnd);
            for (itagc = 0; itagc < psse->ctagc; itagc++)
            {
                TAGM::CloseTag(psse->Ptag(itagc));
            }
            _pggsevFrm->Delete(isevSnd);
            _isevFrmLim--;
        }
    }
    else // 3DMMv1.0: we're queueing
    {
        // 3DMMv1.0: Add this sound to isevSnd
        psseOld = SSE::PsseDupFromGg(_pggsevFrm, isevSnd);
        if (pvNil == psseOld)
        {
            return fFalse;
        }
        // 3DMMv1.0: ctag == 1
        if (prgtag[0].sid == ksidUseCrf)
        {
            if (!_pmvie->FChidFromUserSndCno(prgtag[0].cno, &chid))
            {
                ReleasePpsse(&psseOld);
                return fFalse;
            }
        }
        else
            TrashVar(&chid);

        itagBase = psseOld->ctagc;
        psseNew = psseOld->PsseAddTagChid(prgtag, chid);
        if (pvNil == psseNew)
        {
            ReleasePpsse(&psseOld);
            return fFalse;
        }
        if (!_pggsevFrm->FPut(isevSnd, psseNew->Cb(), psseNew))
        {
            ReleasePpsse(&psseOld);
            ReleasePpsse(&psseNew);
            return fFalse;
        }
        ReleasePpsse(&psseOld);
    }

    //
    // 3DMMv1.0: Play only these sounds
    //
    for (itag = 0; itag < ctag; itag++)
    {
        ptag = &(prgtag[itag]);
        if (ptag->sid == ksidUseCrf)
        {
            if (!Pmvie()->FResolveSndTag(ptag, *(psseNew->Pchid(itag + itagBase))))
                continue;
        }

        pmsnd = (PMSND)vptagm->PbacoFetch(ptag, MSND::FReadMsnd);
        if (pvNil == pmsnd)
            continue;

        // 3DMMv1.0: Only queue if it's not the first sound.
        Pmvie()->Pmsq()->FEnqueue(pmsnd, 0, fLoop, (itag != 0), vlm, pmsnd->Spr(fLoop ? toolLooper : toolSounder),
                                  fFalse, 0);

        ReleasePpo(&pmsnd);
    }

    if (!_FPlaySev(&sev, psseNew, _grfscen | fscenSounds))
    {
        // 3DMMv1.0: non-fatal error...ignore it
    }
    FreePpv((void **)&psseNew); // 3DMMv1.0: don't ReleasePpsse because GG got the tags

    _MarkMovieDirty();
    Pmvie()->Pmcc()->SetSndFrame(fTrue);
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds a sound to the event list for this frame.
 * Note: The chid's in the tagc are current
 *
 * Parameters:
 *  fLoop - should the sound loop?
 *  fQueue - queue after existing sounds, or replace them?
 *  vlm - volume to play this sound at
 *  sty - sound type (midi, speech, or SFX)
 *  ctag - number of sounds
 *  prgtagc - array of MSND tags
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FAddSndCoreTagc(bool fLoop, bool fQueue, int32_t vlm, int32_t sty, int32_t ctagc, PTAGC prgtagc)
{
    AssertThis(0);
    AssertPvCb(prgtagc, LwMul(ctagc, SIZEOF(TAGC)));
    Assert(!fQueue || (ctagc == 1), "if fQueue'ing, you should only be adding one sound");
    Assert(!fQueue || !fLoop, "can't both queue and loop");
    AssertIn(sty, 0, styLim);

    SEV sev;
    PSSE psseOld;
    PSSE psseNew;
    int32_t isev;
    int32_t isevSnd = ivNil;

    //
    // 3DMMv1.0: Find any other sevtPlaySnd events in this frame with the same sty
    //
    for (isev = _isevFrmLim - 1; isev >= 0; isev--)
    {
        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.nfrm != _nfrmCur)
            break;
        if (sev.sevt == sevtPlaySnd)
        {
            psseOld = (PSSE)_pggsevFrm->QvGet(isev);
            if (psseOld->sty == sty)
            {
                // 3DMMv1.0: Found a match, which we will either add to or replace
                isevSnd = isev;
                break;
            }
        }
    }

    if (isevSnd == ivNil)
    {
        fQueue = fFalse; // 3DMMv1.0: nothing to queue to
    }

    sev.nfrm = _nfrmCur;
    sev.sevt = sevtPlaySnd;

    if (!fQueue)
    {
        // 3DMMv1.0: Create new event, replace any old event of same sty
        psseNew = SSE::PsseNew(vlm, sty, fLoop, ctagc, prgtagc);
        if (pvNil == psseNew)
            return fFalse;
        if (!_FAddSev(&sev, psseNew->Cb(), psseNew))
        {
            ReleasePpsse(&psseNew);
            return fFalse;
        }
        if (isevSnd != ivNil)
        {
            // 3DMMv1.0: Delete old event, if any
            PSSE psse;
            int32_t itagc;
            psse = (PSSE)_pggsevFrm->QvGet(isevSnd);
            for (itagc = 0; itagc < psse->ctagc; itagc++)
            {
                TAGM::CloseTag(psse->Ptag(itagc));
            }
            _pggsevFrm->Delete(isevSnd);
            _isevFrmLim--;
        }
    }
    else // 3DMMv1.0: we're queueing
    {
        Bug("Should never queue when undoing");
    }

    if (!_FPlaySev(&sev, psseNew, _grfscen))
    {
        // 3DMMv1.0: non-fatal error...ignore it
    }

    FreePpv((void **)&psseNew); // 3DMMv1.0: don't ReleasePpsse because GG got the tags
    _MarkMovieDirty();
    Pmvie()->Pmcc()->SetSndFrame(fTrue);
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds a sound to the event list for this frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *	ptag - pointer to the tag for the sound.
 *  fLoop - whether to loop the sound
 *  fQueue - queue after existing sounds, or replace them?
 *  vlm - volume to play this sound at
 *  sty - sound type (midi, speech, or SFX)
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FAddSnd(PTAG ptag, bool fLoop, bool fQueue, int32_t vlm, int32_t sty)
{
    AssertThis(0);
    AssertVarMem(ptag);
    Assert(!fQueue || !fLoop, "can't both queue and loop");
    AssertIn(sty, 0, styLim);

    PSUNS psuns;
    PSSE psse;
    bool fFound;

    // 3DMMv1.0: Create a SUNS with nil _psse
    psuns = SUNS::PsunsNew();

    if (psuns == pvNil)
    {
        return (fFalse);
    }
    psuns->SetSty(sty);

    // 3DMMv1.0: grab sound before edit, if any
    if (!FGetSnd(sty, &fFound, &psse))
    {
        ReleasePpo(&psuns);
        return fFalse;
    }
    if (fFound)
    {
        if (!psuns->FSetSnd(psse))
        {
            ReleasePpsse(&psse);
            ReleasePpo(&psuns);
            return fFalse;
        }
        ReleasePpsse(&psse);
    }

    if (!_pmvie->FAddUndo(psuns))
    {
        ReleasePpo(&psuns);
        return (fFalse);
    }

    ReleasePpo(&psuns);

    if (!FAddSndCore(fLoop, fQueue, vlm, sty, 1, ptag))
    {
        _pmvie->ClearUndo();
        return (fFalse);
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine removes a sound from the event list for this frame.
 *
 * Parameters:
 *	sty - the type of sound to remove
 *
 *
 * Returns:
 *  None
 *
 ****************************************************/
void SCEN::RemSndCore(int32_t sty)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);

    PSEV qsev;
    int32_t isev;

    //
    // 3DMMv1.0: Find the sound
    //
    for (isev = _isevFrmLim - 1; isev >= 0; isev--)
    {
        qsev = (PSEV)_pggsevFrm->QvFixedGet(isev);

        if (qsev->nfrm != _nfrmCur)
        {
            Bug("sty not found");
            return;
        }

        if ((qsev->sevt == sevtPlaySnd) && ((PSSE)_pggsevFrm->QvGet(isev))->sty == sty)
        {
            //
            // 3DMMv1.0: Remove it
            //
            PSSE psse;
            int32_t itagc;
            psse = (PSSE)_pggsevFrm->QvGet(isev);
            for (itagc = 0; itagc < psse->ctagc; itagc++)
            {
                TAGM::CloseTag(psse->Ptag(itagc));
            }
            _pggsevFrm->Delete(isev);
            _isevFrmLim--;

            if (sty == styMidi)
            {
                ReleasePpsse(&_psseBkgd);
            }

            UpdateSndFrame();

            _MarkMovieDirty();
            return;
        }
    }

    Bug("No such sound");
}

/** 3DMMv1.0: **************************************************
 *
 * This routine removes a sound from the event list for this frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *	sty - the type of sound to remove
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FRemSnd(int32_t sty)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);

    PSUNS psuns;
    PSSE psse = pvNil;
    int32_t isev;
    PSEV qsev;

    psuns = SUNS::PsunsNew();

    if (psuns == pvNil)
    {
        return (fFalse);
    }

    //
    // 3DMMv1.0: Find the sound
    //
    for (isev = _isevFrmLim - 1; isev >= 0; isev--)
    {
        qsev = (PSEV)_pggsevFrm->QvFixedGet(isev);

        if (qsev->nfrm != _nfrmCur)
        {
            Bug("sty not found");
            return fTrue;
        }

        if ((qsev->sevt == sevtPlaySnd) && ((PSSE)_pggsevFrm->QvGet(isev))->sty == sty)
        {
            psse = SSE::PsseDupFromGg(_pggsevFrm, isev);
            if (psse == pvNil)
            {
                return fFalse;
            }
        }
    }
    if (!psuns->FSetSnd(psse))
    {
        ReleasePpsse(&psse);
        return fFalse;
    }
    ReleasePpsse(&psse);

    if (!_pmvie->FAddUndo(psuns))
    {
        ReleasePpo(&psuns);
        return (fFalse);
    }

    ReleasePpo(&psuns);

    RemSndCore(sty);

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine finds a specific sound in the current frame.
 *
 * Parameters:
 *	sty - sound type to search for
 *  pfFound - set to fTrue if a sound is found, else fFalse
 *  ppsse - gets a pointer to the SSE if it is found
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FGetSnd(int32_t sty, bool *pfFound, PSSE *ppsse)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    AssertVarMem(ppsse);

    PSEV qsev;
    int32_t isev;

    *pfFound = fFalse;
    //
    // 3DMMv1.0: Check event list.
    //
    for (isev = _isevFrmLim - 1; isev >= 0; isev--)
    {
        qsev = (PSEV)_pggsevFrm->QvFixedGet(isev);

        if (qsev->nfrm != _nfrmCur)
        {
            break; // 3DMMv1.0: sound not found
        }

        if (qsev->sevt == sevtPlaySnd)
        {
            if (sty == ((PSSE)_pggsevFrm->QvGet(isev))->sty)
            {
                *ppsse = SSE::PsseDupFromGg(_pggsevFrm, isev);
                if (*ppsse == pvNil)
                {
                    return fFalse; // 3DMMv1.0: memory error
                }
                *pfFound = fTrue;
                return fTrue;
            }
        }
    }

    //
    // 3DMMv1.0: End of list...sound not found
    //
    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine plays the background sound, if any.
 * If the sound really started at an earlier frame,
 * this function tries to resume the sound at the
 * appropriate offset into the sound (dtsStart).
 *
 * Parameters:
 *  none
 *
 * Returns:
 *  none
 *
 ****************************************************/
void SCEN::PlayBkgdSnd(void)
{
    AssertThis(0);

    uint32_t dtsStart;
    uint32_t dfrm;

    if (pvNil != _psseBkgd)
    {
        dfrm = Nfrm() - _nfrmSseBkgd;
        Assert(dfrm >= 0, "this sound is in the future!");
        dtsStart = LwMulDiv(dfrm, kdtsSecond, kfps);
        _psseBkgd->PlayAllSounds(Pmvie(), dtsStart);
    }
}

/** 3DMMv1.0: **************************************************
 *
 * This routine queries what sounds are attched to the
 * scene at the current frame
 *
 * Parameters:
 *  sty - the sound type to query
 *
 * Returns:
 *  fFalse if an error occurs
 *
 ****************************************************/
bool SCEN::FQuerySnd(int32_t sty, PGL *ppgltagSnd, int32_t *pvlm, bool *pfLoop)
{
    AssertThis(0);
    AssertVarMem(ppgltagSnd);
    AssertVarMem(pvlm);
    AssertVarMem(pfLoop);

    PSSE psse;
    bool fFound;
    int32_t itag;

    *ppgltagSnd = pvNil;

    if (!FGetSnd(sty, &fFound, &psse))
    {
        return fFalse; // 3DMMv1.0: error
    }
    if (!fFound)
    {
        return fTrue; // 3DMMv1.0: no sounds (*ppglTagSnd is nil)
    }
    *ppgltagSnd = GL::PglNew(SIZEOF(TAG), psse->ctagc);
    if (pvNil == *ppgltagSnd)
    {
        ReleasePpsse(&psse);
        return fFalse;
    }
    AssertDo((*ppgltagSnd)->FSetIvMac(psse->ctagc), "PglNew should have ensured space");
    for (itag = 0; itag < psse->ctagc; itag++)
    {
        (*ppgltagSnd)->Put(itag, psse->Ptag(itag));
    }
    *pvlm = psse->vlm;
    *pfLoop = psse->fLoop;
    ReleasePpsse(&psse);

    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine changes the volume of the sound of
 * type sty at the current frame to vlmNew
 *
 * Parameters:
 *  sty - the sound type to query
 *  vlmNew - the new volume
 *
 * Returns:
 *  none
 *
 ****************************************************/
void SCEN::SetSndVlmCore(int32_t sty, int32_t vlmNew)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    AssertIn(vlmNew, 0, kvlmFull + 1);

    PSEV qsev;
    int32_t isev;
    PSSE psse;

    //
    // 3DMMv1.0: Check event list.
    //
    for (isev = _isevFrmLim - 1; isev >= 0; isev--)
    {
        qsev = (PSEV)_pggsevFrm->QvFixedGet(isev);

        if (qsev->nfrm != _nfrmCur)
        {
            Bug("No such sound");
            break; // 3DMMv1.0: sound not found
        }

        if (qsev->sevt == sevtPlaySnd)
        {
            psse = ((PSSE)_pggsevFrm->QvGet(isev));
            if (sty == psse->sty)
            {
                psse->vlm = vlmNew;
                _MarkMovieDirty();
                return;
            }
        }
    }

    //
    // 3DMMv1.0: End of list...sound not found
    //
    Bug("No such sound");
}

/** 3DMMv1.0: ****************************************************************************
    UpdateSndFrame
        Enumerates all scene events for the current frame, and asks all actors
        to enumerate all of their actor events for the current frame, looking
        for a sound event.  Has the movie's MCC update the frame-sound state
        based on the results of the search.
************************************************************ PETED ***********/
void SCEN::UpdateSndFrame(void)
{
    bool fSoundInFrame = fFalse;
    int32_t iv = _isevFrmLim;

    while (iv-- > 0)
    {
        PSEV qsev = (PSEV)_pggsevFrm->QvFixedGet(iv);

        if (qsev->nfrm != _nfrmCur)
        {
            iv = -1;
            break;
        }

        if (qsev->sevt == sevtPlaySnd)
        {
            fSoundInFrame = fTrue;
            goto LDone;
        }
    }

    while (++iv < _pglpactr->IvMac())
    {
        PACTR pactr;

        _pglpactr->Get(iv, &pactr);
        AssertPo(pactr, 0);

        if (pactr->FSoundInFrm())
        {
            fSoundInFrame = fTrue;
            goto LDone;
        }
    }

LDone:
    Pmvie()->Pmcc()->SetSndFrame(fSoundInFrame);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds an event to the current frame of the current scene.
 *
 * Parameters:
 *  psev - Pointer to the event to add.
 *	cbVar- Size of pvVar buffer, in bytes.
 *  pvVar- Pointer to the variable part of the event.
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::_FAddSev(PSEV psev, int32_t cbVar, void *pvVar)
{
    AssertThis(0);
    AssertVarMem(psev);
    AssertIn(cbVar, 0, klwMax);

    bool fRetValue;

    //
    // 3DMMv1.0: Add the event to the scene
    //
    _MarkMovieDirty();

    fRetValue = _pggsevFrm->FInsert(_isevFrmLim++, cbVar, pvVar, psev);

    if (!fRetValue)
    {
        _isevFrmLim--;
    }

    return (fRetValue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine sets the selected actor to the given one.
 *
 * Parameters:
 *  pactr - Pointer to the actr to select.  pvNil is a
 *  	valid value and deselects the current actor and tbox.
 *
 * Returns:
 *	None
 *
 ****************************************************/
void SCEN::SelectActr(ACTR *pactr)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);

    MODERN_BR_SCENE_LOG("SCEN::SelectActr BEGIN scene=%p incoming=%p current=%p count=%ld tbox=%p",
                        this, pactr, _pactrSelected, (long)CactrSelected(), _ptboxSelected);

    // A designated light attachment is not viewport-pickable while hidden.
    // Therefore, if one of those actors arrives here as a selection it came
    // from a list/browser. Reveal all designated attachments before selecting
    // it, exactly as the CTRL+ALT+L mode contract specifies.
    if (pactr != pvNil && Pmvie()->FIsLightAttachmentHidden(pactr->Arid()))
        Pmvie()->FSetSceneHideLightObjects(Pmvie()->Iscen(), fFalse, fTrue);

    PMVU pmvu = (PMVU)Pmvie()->PddgGet(0);
    AssertNilOrPo(pmvu, 0);

    if ((pmvu != pvNil) && !pmvu->FTextMode())
    {
        if (_pglaridSelected != pvNil)
        {
            for (int32_t iactr = 0; iactr < _pglaridSelected->IvMac(); iactr++)
            {
                int32_t arid;
                _pglaridSelected->Get(iactr, &arid);
                PACTR pactrOld = PactrFromArid(arid);
                if (pactrOld != pvNil && pactrOld != pactr)
                {
                    MODERN_BR_SCENE_LOG("SCEN::SelectActr unhilite actor=%p arid=%ld",
                                        pactrOld, (long)arid);
                    pactrOld->Unhilite();
                }
            }
        }

        if (pactr != pvNil)
            pactr->Hilite();

        if (_ptboxSelected != pvNil)
            _ptboxSelected->Select(fFalse);
    }

    if (_pglaridSelected != pvNil)
    {
        _pglaridSelected->FSetIvMac(0);
        if (pactr != pvNil)
        {
            int32_t arid = pactr->Arid();
            AssertDo(_pglaridSelected->FAdd(&arid), "Could not store selected actor ARID");
        }
    }

    _pactrSelected = pactr;
    _pactrSelected2 = pvNil;
    _pmvie->InvalViews();
    MVIE::LightEditorLog(_pmvie, "scene_select actor_ptr=%p arid=%ld count=%ld",
                         (void *)pactr, pactr != pvNil ? (long)pactr->Arid() : (long)aridNil,
                         (long)CactrSelected());
    _pmvie->BuildActionMenu();
    MODERN_BR_SCENE_LOG("SCEN::SelectActr END scene=%p selected=%p count=%ld incoming_body=%p incoming_arid=%ld",
                        this, _pactrSelected, (long)CactrSelected(),
                        pactr != pvNil ? pactr->Pbody() : pvNil,
                        pactr != pvNil ? (long)pactr->Arid() : (long)aridNil);
    REFRESH_4DMM_OBJECT_GROUP_SELECTION_UI();
}

/****************************************************
 * Shift-add another scene object to the current -multi selection.
 * Actors, props and 3D Words intentionally share the same ACTR/ARID
 * selection list so mixed object groups are possible.
 ****************************************************/
void SCEN::SelectActrAdd(ACTR *pactr)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);

    if (pactr == pvNil)
        return;

    if (_pactrSelected == pvNil || _pglaridSelected == pvNil || _pglaridSelected->IvMac() == 0)
    {
        SelectActr(pactr);
        return;
    }

    if (FActrSelected(pactr->Arid()))
    {
        // Keep the full multi-selection, but make the object the user actually
        // clicked the primary actor for the hand/reposition drag and mouse-up
        // cursor return. v97 left the first selected object primary forever,
        // which made every later Shift-click snap back to object #1.
        _pactrSelected = pactr;
        _pactrSelected2 = pvNil;
        for (int32_t iactr = 0; iactr < CactrSelected(); iactr++)
        {
            PACTR pactrT = PactrSelectedAt(iactr);
            if (pactrT != pvNil && pactrT != _pactrSelected)
            {
                _pactrSelected2 = pactrT;
                break;
            }
        }
        _pmvie->BuildActionMenu();
        REFRESH_4DMM_OBJECT_GROUP_SELECTION_UI();
        return;
    }

    if (Pmvie()->FIsLightAttachmentHidden(pactr->Arid()))
        Pmvie()->FSetSceneHideLightObjects(Pmvie()->Iscen(), fFalse, fTrue);

    int32_t arid = pactr->Arid();
    if (!_pglaridSelected->FAdd(&arid))
        return;

    PMVU pmvu = (PMVU)Pmvie()->PddgGet(0);
    AssertNilOrPo(pmvu, 0);
    if ((pmvu != pvNil) && !pmvu->FTextMode())
    {
        pactr->Hilite();
        if (_ptboxSelected != pvNil)
            _ptboxSelected->Select(fFalse);
    }

    // Selection-list order remains stable for grouping, while the clicked
    // object becomes the primary edit target. This preserves the original
    // one-object hand/reposition semantics inside an unlimited selection.
    _pactrSelected = pactr;
    _pactrSelected2 = pvNil;
    for (int32_t iactr = 0; iactr < CactrSelected(); iactr++)
    {
        PACTR pactrT = PactrSelectedAt(iactr);
        if (pactrT != pvNil && pactrT != _pactrSelected)
        {
            _pactrSelected2 = pactrT;
            break;
        }
    }
    _pmvie->InvalViews();
    _pmvie->BuildActionMenu();
    MODERN_BR_SCENE_LOG("SCEN::SelectActrAdd scene=%p actor=%p arid=%ld count=%ld",
                        this, pactr, (long)arid, (long)CactrSelected());
    REFRESH_4DMM_OBJECT_GROUP_SELECTION_UI();
}

/****************************************************
 * Remove one actor/prop/3D Word from the current -multi selection.
 * Remaining selected objects stay highlighted.
 ****************************************************/
void SCEN::SelectActrRemove(ACTR *pactr)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);

    if (pactr == pvNil || _pglaridSelected == pvNil)
        return;

    const int32_t aridRemove = pactr->Arid();
    int32_t iactrRemove = ivNil;
    for (int32_t iactr = 0; iactr < _pglaridSelected->IvMac(); iactr++)
    {
        int32_t arid;
        _pglaridSelected->Get(iactr, &arid);
        if (arid == aridRemove)
        {
            iactrRemove = iactr;
            break;
        }
    }
    if (iactrRemove == ivNil)
        return;

    PMVU pmvu = (PMVU)Pmvie()->PddgGet(0);
    AssertNilOrPo(pmvu, 0);
    if ((pmvu != pvNil) && !pmvu->FTextMode())
        pactr->Unhilite();

    _pglaridSelected->Delete(iactrRemove);
    const int32_t cSelected = _pglaridSelected->IvMac();
    _pactrSelected = cSelected > 0 ? PactrSelectedAt(cSelected - 1) : pvNil;
    _pactrSelected2 = pvNil;
    for (int32_t iactr = 0; iactr < cSelected; iactr++)
    {
        PACTR pactrT = PactrSelectedAt(iactr);
        if (pactrT != pvNil && pactrT != _pactrSelected)
        {
            _pactrSelected2 = pactrT;
            break;
        }
    }

    _pmvie->InvalViews();
    _pmvie->BuildActionMenu();
    MODERN_BR_SCENE_LOG("SCEN::SelectActrRemove scene=%p actor=%p arid=%ld count=%ld primary=%p",
                        this, pactr, (long)aridRemove, (long)cSelected, _pactrSelected);
    REFRESH_4DMM_OBJECT_GROUP_SELECTION_UI();
}

int32_t SCEN::CactrSelected(void)
{
    AssertThis(0);
    return _pglaridSelected == pvNil ? 0 : _pglaridSelected->IvMac();
}

PACTR SCEN::PactrSelectedAt(int32_t iactr)
{
    AssertThis(0);
    if (_pglaridSelected == pvNil || !FIn(iactr, 0, _pglaridSelected->IvMac()))
        return pvNil;
    int32_t arid;
    _pglaridSelected->Get(iactr, &arid);
    return PactrFromArid(arid);
}

bool SCEN::FActrSelected(int32_t arid)
{
    AssertThis(0);
    if (_pglaridSelected == pvNil)
        return fFalse;
    for (int32_t iactr = 0; iactr < _pglaridSelected->IvMac(); iactr++)
    {
        int32_t aridT;
        _pglaridSelected->Get(iactr, &aridT);
        if (aridT == arid)
            return fTrue;
    }
    return fFalse;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine sets the selected text box to the given one.
 *
 * Parameters:
 *  ptbox - Pointer to the tbox to select.  pvNil is a
 *  	valid value and deselects the current actor and tbox.
 *
 * Returns:
 *	None
 *
 ****************************************************/
void SCEN::SelectTbox(PTBOX ptbox)
{
    AssertThis(0);
    AssertNilOrPo(ptbox, 0);

    PMVU pmvu;

    pmvu = (PMVU)Pmvie()->PddgGet(0);
    AssertNilOrPo(pmvu, 0);

    _pmvie->InvalViews();

    if ((pmvu != pvNil) && pmvu->FTextMode())
    {

        if (_pglaridSelected != pvNil)
        {
            for (int32_t iactr = 0; iactr < _pglaridSelected->IvMac(); iactr++)
            {
                int32_t arid;
                _pglaridSelected->Get(iactr, &arid);
                PACTR pactrOld = PactrFromArid(arid);
                if (pactrOld != pvNil)
                    pactrOld->Unhilite();
            }
            _pglaridSelected->FSetIvMac(0);
        }
        _pactrSelected = pvNil;
        _pactrSelected2 = pvNil;
        _pmvie->BuildActionMenu();

        if ((ptbox == _ptboxSelected) && ((ptbox == pvNil) || ptbox->FSelected()))
        {
            return;
        }

        if (pvNil != _ptboxSelected)
        {
            _ptboxSelected->Select(fFalse);
        }

        if (pvNil != ptbox)
        {
            ptbox->Select(fTrue);
        }
    }

    _ptboxSelected = ptbox;
    _pmvie->Pmcc()->TboxSelected();
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds an actor to the scene at the current frame,
 * if the arid is aridNil, else it replaces the actor in the
 * scene with the same arid.
 *
 * Parameters:
 *  pactr - Pointer to the actr to add.
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FAddActrCore(ACTR *pactr)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    PSEV qsev;
    SEV sev;
    int32_t isev;
    int32_t ipactr;
    STN stn;
    PACTR pactrOld;
    bool fRetValue;

    MODERN_BR_SCENE_LOG("SCEN::FAddActrCore BEGIN scene=%p actor=%p arid=%ld body=%p frame=%ld start_events=%ld scene_roll=%ld selected=%p selected2=%p",
                        this, pactr, (long)pactr->Arid(), pactr->Pbody(), (long)_nfrmCur,
                        (long)_pggsevStart->IvMac(), (long)_pglpactr->IvMac(),
                        _pactrSelected, _pactrSelected2);

    //
    // 3DMMv1.0: Check if actor is in Scene already.
    //
    for (isev = 0; isev < _pggsevStart->IvMac(); isev++)
    {
        qsev = (PSEV)_pggsevStart->QvFixedGet(isev);

        if (qsev->sevt != sevtAddActr)
        {
            continue;
        }

        _pggsevStart->Get(isev, (void *)&pactrOld);

        if (pactrOld->Arid() != pactr->Arid())
        {
            continue;
        }

        MODERN_BR_SCENE_LOG("SCEN::FAddActrCore replacement candidate event=%ld incoming=%p old=%p arid=%ld",
                            (long)isev, pactr, pactrOld, (long)pactr->Arid());

        //
        // 3DMMv1.0: Replace actor in scene roll call
        //
        for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
        {
            _pglpactr->Get(ipactr, &pactrOld);

            if (pactrOld->Arid() == pactr->Arid())
            {

                pactr->AddRef();
                pactr->SetPscen(this);
                if (pactr->Pbody() == pvNil)
                {
                    TAG tagTmpl;
                    pactr->GetTagTmpl(&tagTmpl);
                    MODERN_BR_SCENE_LOG("SCEN::FAddActrCore replacement FAIL create_body actor=%p arid=%ld sid=%ld ctg=0x%08lX cno=%ld",
                                        pactr, (long)pactr->Arid(), (long)tagTmpl.sid,
                                        (unsigned long)tagTmpl.ctg, (long)tagTmpl.cno);
                    MVIE::MultiLog(_pmvie,
                        "actor_insert FAIL stage=create_body_replace arid=%ld sid=%ld ctg=0x%08lX cno=%ld",
                        (long)pactr->Arid(), (long)tagTmpl.sid,
                        (unsigned long)tagTmpl.ctg, (long)tagTmpl.cno);
                    ReleasePpo(&pactr);
                    return fFalse;
                }

                //
                // 3DMMv1.0: Plop them into this frame
                //
                if (!pactr->FGotoFrame(_nfrmCur))
                {
                    ReleasePpo(&pactr);
                    return (fFalse);
                }

                //
                // 3DMMv1.0: Replace starting actor
                //
                const bool fLightAttachmentWasHidden = _pmvie->FIsLightAttachmentHidden(pactr->Arid());
                _pggsevStart->Put(isev, &pactr);
                _pglpactr->Put(ipactr, &pactr);

                // SelectActr() unhilites the previous selection before installing
                // the replacement.  If that selection is pactrOld, releasing it
                // first leaves _pactrSelected pointing at freed storage.  The next
                // SelectActr() then reaches ACTR::Unhilite() through a stale actor
                // and can call BODY::Unhilite() with a null body.  Clear that
                // ownership while the old actor is still alive.
                const bool fOldWasSelected = (_pactrSelected == pactrOld);
                const bool fOldWasSelected2 = (_pactrSelected2 == pactrOld);
                if (fOldWasSelected || fOldWasSelected2)
                {
                    MODERN_BR_SCENE_LOG("SCEN::FAddActrCore replacement clearing old selection old=%p body=%p primary=%d secondary=%d",
                                        pactrOld, pactrOld->Pbody(), (int)fOldWasSelected,
                                        (int)fOldWasSelected2);
                    if (pactrOld->Pbody() != pvNil)
                        pactrOld->Unhilite();
                    else
                        MODERN_BR_SCENE_LOG("SCEN::FAddActrCore replacement old selected actor has NULL body old=%p",
                                            pactrOld);
                    if (fOldWasSelected)
                        _pactrSelected = pvNil;
                    if (fOldWasSelected2)
                        _pactrSelected2 = pvNil;
                }

                // Rebind light/hide ownership before selecting the replacement.
                // Otherwise SelectActr() can interpret an internal undo swap of
                // a hidden light attachment as a user list selection and reveal
                // every hidden light object as a side effect.
                MODERN_BR_SCENE_LOG("SCEN::FAddActrCore replacement releasing old=%p incoming=%p arid=%ld",
                                    pactrOld, pactr, (long)pactr->Arid());
                pactrOld->Hide();
                ReleasePpo(&pactrOld);
                _pmvie->ActorBodyReplaced(pactr->Arid());
                if (!fLightAttachmentWasHidden)
                    SelectActr(pactr);
                else
                    SelectActr(pvNil);
                MODERN_BR_SCENE_LOG("SCEN::FAddActrCore replacement OK incoming=%p arid=%ld scene_roll=%ld selected=%p",
                                    pactr, (long)pactr->Arid(), (long)_pglpactr->IvMac(),
                                    _pactrSelected);
                InvalFrmRange();
                _MarkMovieDirty();
                UpdateSndFrame();
                return (fTrue);
            }
        }

        Bug("Cannot find actor in Roll call");
    }

    //
    // 3DMMv1.0: Add actor to inital list of events to do.
    //
    sev.sevt = sevtAddActr;
    fRetValue = _pggsevStart->FInsert(0, SIZEOF(PACTR), &pactr, &sev);
    MODERN_BR_SCENE_LOG("SCEN::FAddActrCore new FInsert result=%d actor=%p arid=%ld start_events=%ld",
                        (int)fRetValue, pactr, (long)pactr->Arid(), (long)_pggsevStart->IvMac());

    if (fRetValue)
    {
        pactr->AddRef();

        pactr->Ptmpl()->GetName(&stn);
        const bool fMovieRollCall = _pmvie->FAddToRollCall(pactr, &stn);
        MODERN_BR_SCENE_LOG("SCEN::FAddActrCore movie rollcall result=%d actor=%p arid=%ld",
                            (int)fMovieRollCall, pactr, (long)pactr->Arid());
        if (!fMovieRollCall)
        {
            _pggsevStart->Delete(0);
            ReleasePpo(&pactr);
            MODERN_BR_SCENE_LOG("SCEN::FAddActrCore FAIL movie rollcall");
            return (fFalse);
        }

        fRetValue = _FPlaySev(&sev, &pactr, _grfscen);
        MODERN_BR_SCENE_LOG("SCEN::FAddActrCore _FPlaySev result=%d actor=%p arid=%ld scene_roll=%ld",
                            (int)fRetValue, pactr, (long)pactr->Arid(), (long)_pglpactr->IvMac());

        if (!fRetValue)
        {
            _pmvie->RemFromRollCall(pactr);
            _pggsevStart->Delete(0);
            ReleasePpo(&pactr);
        }
        else
        {
            InvalFrmRange();
            _MarkMovieDirty();
            UpdateSndFrame();
        }
    }

    MODERN_BR_SCENE_LOG("SCEN::FAddActrCore END result=%d actor=%p arid=%ld start_events=%ld scene_roll=%ld",
                        (int)fRetValue, pactr,
                        pactr != pvNil ? (long)pactr->Arid() : (long)aridNil,
                        (long)_pggsevStart->IvMac(), (long)_pglpactr->IvMac());
    return (fRetValue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds an actor to the scene at the current frame.
 * This auto magically selects the actor and places them on stage.
 *
 * Parameters:
 *  pactr - Pointer to the actr to add.
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FAddActr(ACTR *pactr)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    MODERN_BR_SCENE_LOG("SCEN::FAddActr BEGIN scene=%p actor=%p arid=%ld body=%p frame=%ld",
                        this, pactr, (long)pactr->Arid(), pactr->Pbody(), (long)_nfrmCur);

    if (!FAddActrCore(pactr))
    {
        MODERN_BR_SCENE_LOG("SCEN::FAddActr FAIL FAddActrCore actor=%p arid=%ld",
                            pactr, (long)pactr->Arid());
        return (fFalse);
    }

    MODERN_BR_SCENE_LOG("SCEN::FAddActr FAddOnStageCore BEGIN actor=%p arid=%ld body=%p",
                        pactr, (long)pactr->Arid(), pactr->Pbody());
    const bool fOnStageAdded = pactr->FAddOnStageCore();
    MODERN_BR_SCENE_LOG("SCEN::FAddActr FAddOnStageCore result=%d actor=%p arid=%ld body=%p onstage=%d",
                        (int)fOnStageAdded, pactr, (long)pactr->Arid(), pactr->Pbody(),
                        (int)pactr->FOnStage());
    if (!fOnStageAdded)
    {
        RemActrCore(pactr->Arid());
        MODERN_BR_SCENE_LOG("SCEN::FAddActr FAIL removed actor after FAddOnStageCore failure actor=%p",
                            pactr);
        return (fFalse);
    }

    SelectActr(pactr);
    MODERN_BR_SCENE_LOG("SCEN::FAddActr SUCCESS actor=%p arid=%ld selected=%p scene_roll=%ld",
                        pactr, (long)pactr->Arid(), _pactrSelected, (long)_pglpactr->IvMac());

    //
    // 3DMMv1.0: The MVU creates the undo object for this because of the mouse
    // 3DMMv1.0: placement.
    //
    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine removes an actor from the scene.
 *
 * Parameters:
 *  arid - The actor id to remove.
 *
 *
 * Returns:
 *  None
 *
 ****************************************************/
void SCEN::RemActrCore(int32_t arid)
{
    AssertThis(0);

    PSEV qsev;
    PACTR pactrTmp;
    int32_t isev;

    //
    // 3DMMv1.0: Check if actor is in Scene already.
    //
    for (isev = 0; isev < _pggsevStart->IvMac(); isev++)
    {
        qsev = (PSEV)_pggsevStart->QvFixedGet(isev);

        if (qsev->sevt != sevtAddActr)
        {
            continue;
        }

        _pggsevStart->Get(isev, &pactrTmp);
        if (pactrTmp->Arid() != arid)
        {
            continue;
        }

        //
        // 3DMMv1.0: Remove actor from inital list of events to do.
        //
        _pggsevStart->Delete(isev);

        //
        // 3DMMv1.0: Remove actor from the scene roll call
        //
        for (isev = 0; isev < _pglpactr->IvMac(); isev++)
        {
            _pglpactr->Get(isev, &pactrTmp);

            if (pactrTmp->Arid() != arid)
            {
                continue;
            }

            _pglpactr->Delete(isev);

            //
            // 3DMMv1.0: Remove actor as the currently selected actor
            //
            if (FActrSelected(pactrTmp->Arid()))
            {
                PMVU pmvu;
                SelectActr(pvNil);
                pmvu = (PMVU)_pmvie->PddgGet(0);
                if (pmvu != pvNil)
                    pmvu->EndPlaceActor();
            }

            //
            // 3DMMv1.0: Remove actor from the movie roll call
            //
            _pmvie->RemFromRollCall(pactrTmp);

            pactrTmp->Hide();

            ReleasePpo(&pactrTmp);
            _MarkMovieDirty();
            UpdateSndFrame();

            return;
        }

        Bug("Actor does not exist in roll call");
        _MarkMovieDirty();
        return;
    }

    Bug("Actor does not exist in scene");
}

/** 3DMMv1.0: **************************************************
 *
 * This routine removes an actor from the scene, and
 * creates an undo object for the action.
 *
 * Parameters:
 *  arid - The actor id to remove.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FRemActr(int32_t arid)
{
    AssertThis(0);
    AssertIn(arid, 0, 500);

    PSUNA psuna;
    int32_t ipactr;
    PACTR pactr;

    //
    // 3DMMv1.0: Find the actor for undo purposes
    //
    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        if (pactr->Arid() == arid)
        {
            break;
        }
    }

    AssertPo(pactr, 0);
    pactr->AddRef();

    psuna = SUNA::PsunaNew();

    if (psuna == pvNil)
    {
        ReleasePpo(&pactr);
        return (fFalse);
    }

    psuna->SetActr(pactr);
    psuna->SetType(utDel);

    if (!_pmvie->FAddUndo(psuna))
    {
        return (fFalse);
    }

    ReleasePpo(&psuna);

    RemActrCore(arid);

    return (fTrue);
}

static bool _F4DMMBodySelectableForScenePick(PBODY pbody, void *pvContext)
{
    PSCEN pscen = (PSCEN)pvContext;
    if (pscen == pvNil || pbody == pvNil)
        return fTrue;
    PGL pglpactr = pscen->PglRollCall();
    if (pglpactr == pvNil)
        return fTrue;
    for (int32_t iactr = 0; iactr < pglpactr->IvMac(); ++iactr)
    {
        PACTR pactr = pvNil;
        pglpactr->Get(iactr, &pactr);
        if (pactr != pvNil && pactr->FIsMyBody(pbody))
            return pscen->Pmvie()->FObjectSelectable(pactr->Arid());
    }
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine returns the actor pointed at by the mouse.
 *
 * Parameters:
 *	xp - X position of the mouse within the display space.
 *	yp - Y position of the mouse within the display space.
 *  pibset - Place to store the index of the group of body parts hit.
 *
 * Returns:
 *  Pointer to the actor, pvNil if none.
 *
 ****************************************************/
ACTR *SCEN::PactrFromPt(int32_t xp, int32_t yp, int32_t *pibset)
{
    AssertThis(0);
    AssertVarMem(pibset);

    ACTR *pactr;
    BODY *pbody;
    int32_t ipactr;

    pbody = BODY::PbodyClicked(xp, yp, Pmvie()->Pbwld(), pibset,
                               _F4DMMBodySelectableForScenePick, this);
    if (pvNil == pbody)
    {
        // 4DMM: BrScenePick2D can lose actors scaled beyond the original 10x
        // ceiling.  Only after the normal precise picker reports no hit, use
        // the last rendered 2D bounds of oversized actors as a fallback.
        // Prefer the currently selected oversized actor (so Hired Props
        // selection remains draggable), otherwise choose the smallest matching
        // screen footprint when several giant objects overlap.
        PACTR pactrBest = pvNil;
        int64_t areaBest = INT64_MAX;

        if (_pactrSelected != pvNil && Pmvie()->FObjectSelectable(_pactrSelected->Arid()) &&
            (_pactrSelected->FUsesLargeScalePickFallback() || _pactrSelected->FIsTdt()) &&
            _pactrSelected->FIsInView())
        {
            RC rcSelected;
            _pactrSelected->GetRcBounds(&rcSelected);
            if (rcSelected.FPtIn(xp, yp))
            {
                *pibset = ivNil;
                return _pactrSelected;
            }
        }

        for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
        {
            RC rc;
            int64_t dxp;
            int64_t dyp;
            int64_t area;

            _pglpactr->Get(ipactr, &pactr);
            if (!Pmvie()->FObjectSelectable(pactr->Arid()) ||
                !(pactr->FUsesLargeScalePickFallback() || pactr->FIsTdt()) || !pactr->FIsInView())
                continue;

            pactr->GetRcBounds(&rc);
            if (!rc.FPtIn(xp, yp))
                continue;

            dxp = (int64_t)rc.xpRight - (int64_t)rc.xpLeft;
            dyp = (int64_t)rc.ypBottom - (int64_t)rc.ypTop;
            if (dxp <= 0 || dyp <= 0)
                continue;
            area = dxp * dyp;
            if (pactrBest == pvNil || area < areaBest)
            {
                pactrBest = pactr;
                areaBest = area;
            }
        }

        if (pactrBest != pvNil)
        {
            *pibset = ivNil;
            return pactrBest;
        }

        return pvNil;
    }

    //
    // 3DMMv1.0: loop through actors, call FIsMyBody()
    //
    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        if (pactr->FIsMyBody(pbody))
        {
            return pactr;
        }
    }

    Bug("weird...we clicked a pbody, but we don't know whose it is!");

    return pvNil;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds a text box to the scene at the current frame
 *
 * Parameters:
 *  ptbox - Pointer to the text box to add.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FAddTboxCore(PTBOX ptbox)
{
    AssertThis(0);
    AssertPo(ptbox, 0);

    SEV sev;
    bool fRetValue;

#ifdef DEBUG

    PSEV qsev;
    int32_t isev;

    //
    // 3DMMv1.0: Search for duplicate tbox
    //
    for (isev = 0; isev < _pggsevStart->IvMac(); isev++)
    {
        qsev = (PSEV)_pggsevStart->QvFixedGet(isev);
        if ((qsev->sevt == sevtAddTbox) && (ptbox == (PTBOX)_pggsevStart->QvGet(isev)))
        {
            Bug("Error, adding same text box twice");
            return (fTrue);
        }
    }
#endif

    //
    // 3DMMv1.0: Add text box to event list.
    //
    sev.sevt = sevtAddTbox;
    ptbox->SetScen(this);
    fRetValue = _pggsevStart->FInsert(_pggsevStart->IvMac(), SIZEOF(PTBOX), &ptbox, &sev);

    if (fRetValue)
    {
        ptbox->AddRef();
        fRetValue = _FPlaySev(&sev, &ptbox, _grfscen);

        if (!fRetValue)
        {
            _pggsevStart->Delete(_pggsevStart->IvMac() - 1);
        }
        else
        {
            _MarkMovieDirty();
        }
    }

    return (fRetValue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds a text box to the scene at the current frame
 * creates an undo object for the action.
 *
 * Parameters:
 *  ptbox - Pointer to the text box to add.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FAddTbox(PTBOX ptbox)
{
    PSUNX psunx;
    int32_t itbox;
    int32_t nfrmFirst, nfrmLast;

    psunx = SUNX::PsunxNew();

    if (psunx == pvNil)
    {
        return (fFalse);
    }

    if (!FAddTboxCore(ptbox))
    {
        ReleasePpo(&psunx);
        return (fFalse);
    }

    ptbox->AddRef();
    ptbox->FGetLifetime(&nfrmFirst, &nfrmLast);
    psunx->SetTbox(ptbox); // 3DMMv1.0: transfers the reference count to the undo object
    psunx->SetNfrmFirst(nfrmFirst);
    psunx->SetNfrmLast((nfrmLast == _nfrmLast) ? klwMax : nfrmLast);

    for (itbox = 0; itbox < _pglptbox->IvMac(); itbox++)
    {
        if (ptbox == PtboxFromItbox(itbox))
        {
            psunx->SetItbox(itbox);
            break;
        }
    }

    AssertIn(itbox, 0, _pglptbox->IvMac());

    psunx->SetAdd(fTrue);

    if (!_pmvie->FAddUndo(psunx))
    {
        PushErc(ercSocNotUndoable);
        ReleasePpo(&psunx);
        return (fFalse);
    }

    ReleasePpo(&psunx);
    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine removes a text box from the scene.
 *
 * Parameters:
 *  ptbox - Pointer to the text box to remove.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FRemTboxCore(PTBOX ptbox)
{
    AssertThis(0);
    AssertPo(ptbox, 0);

    PSEV qsev;
    int32_t isev;
    int32_t itbox;
    int32_t nfrmStart, nfrmLast;

    //
    // 3DMMv1.0: Check if currently selected tbox.
    //
    if (ptbox == _ptboxSelected)
    {

        _ptboxSelected = pvNil;
        if (ptbox != pvNil)
        {
            ptbox->Select(fFalse);
        }
    }

    //
    // 3DMMv1.0: Find the text box
    //
    for (isev = 0; isev < _pggsevStart->IvMac(); isev++)
    {
        qsev = (PSEV)_pggsevStart->QvFixedGet(isev);

        if ((qsev->sevt == sevtAddTbox) && ((*(PTBOX *)_pggsevStart->QvGet(isev)) == ptbox))
        {

            //
            // 3DMMv1.0: Remove it.  Do not ReleasePpo() here as reference count
            // 3DMMv1.0: gets transfered to callee.
            //
            _pggsevStart->Delete(isev);

            //
            // 3DMMv1.0: Find it in the _pglptbox
            //
            for (itbox = 0; itbox < _pglptbox->IvMac(); itbox++)
            {
                if (*(PTBOX *)_pglptbox->QvGet(itbox) == ptbox)
                {

                    if (_ptboxSelected == ptbox)
                    {
                        _ptboxSelected = pvNil;
                    }

                    _pglptbox->Delete(itbox);
                    if (ptbox->FGetLifetime(&nfrmStart, &nfrmLast))
                    {
                        //
                        // 3DMMv1.0: This will guarantee that the tbox doesn't leave
                        // 3DMMv1.0: any display on the rendering area.
                        //
                        AssertDo(ptbox->FGotoFrame(nfrmStart - 1), "Could not remove a text box");
                    }
                    ReleasePpo(&ptbox);
                    _MarkMovieDirty();
                    return (fTrue);
                }
            }

            Bug("Text box not found in GL");
            return (fFalse);
        }
    }

    Bug("Error! Could not find text box for removal");
    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine removes a text box from the scene,
 * creates an undo object for the action.
 *
 * Parameters:
 *  ptbox - Pointer to the text box to remove.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FRemTbox(PTBOX ptbox)
{
    AssertThis(0);
    AssertPo(ptbox, 0);

    PSUNX psunx;
    int32_t nfrmFirst, nfrmLast;

    psunx = SUNX::PsunxNew();

    if (psunx == pvNil)
    {
        return (fFalse);
    }

    ptbox->AddRef();
    ptbox->FGetLifetime(&nfrmFirst, &nfrmLast);
    psunx->SetTbox(ptbox); // 3DMMv1.0: transfers the reference count to the undo object
    psunx->SetAdd(fFalse);
    psunx->SetNfrmFirst(nfrmFirst);
    psunx->SetNfrmLast((nfrmLast == _nfrmLast) ? klwMax : nfrmLast);

    if (!_pmvie->FAddUndo(psunx))
    {
        ReleasePpo(&psunx);
        return (fFalse);
    }

    ReleasePpo(&psunx);

    if (!FRemTboxCore(ptbox))
    {
        _pmvie->ClearUndo();
        return (fFalse);
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine gets the ith text box.
 *
 * Parameters:
 *  itbox - Index of the text box to get.
 *
 * Returns:
 *  Pointer to the text box if itbox is valid, else pvNil.
 *
 ****************************************************/
TBOX *SCEN::PtboxFromItbox(int32_t itbox)
{
    AssertThis(0);
    Assert(itbox >= 0, "Bad index value");

    int32_t ipo;
    PTBOX ptbox;

    for (ipo = 0; ipo < _pglptbox->IvMac(); ipo++)
    {

        _pglptbox->Get(ipo, &ptbox);

        itbox--;
        if (itbox == -1)
        {
            break;
        }
    }

    if (itbox != -1)
    {
        ptbox = pvNil;
    }

    return (ptbox);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds/removes a pause to the scene at the current frame.
 *
 * Parameters:
 *	pwit - The pause type, witNil removes a pause, returns old pause type.
 *  pdts - Valid only if wit==witForTime, dts is in clock ticks, returns
 *		old wait time if old wit was witForTime.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FPauseCore(WIT *pwit, int32_t *pdts)
{
    AssertThis(0);
    AssertIn(*pwit, witNil, witLim);
    AssertIn(*pdts, 0, klwMax);

    SEV sev;
    int32_t isev;
    PSEV qsev;
    SEVP sevp;
    WIT witOld;
    int32_t dtsOld;

    //
    // 3DMMv1.0: Start at the first event of this frame.
    //
    for (isev = _isevFrmLim - 1; isev >= 0; isev--)
    {
        qsev = (PSEV)_pggsevFrm->QvFixedGet(isev);

        if (qsev->nfrm != _nfrmCur)
        {
            break;
        }

        //
        // 3DMMv1.0: Find a pause
        //
        if (qsev->sevt == sevtPause)
        {

            //
            // 3DMMv1.0: Replace the event
            //
            _pggsevFrm->Get(isev, &sevp);
            witOld = sevp.wit;
            dtsOld = sevp.dts;

            if (*pwit == witNil)
            {
                _pggsevFrm->Delete(isev);
                _isevFrmLim--;
            }
            else
            {
                sevp.wit = *pwit;
                sevp.dts = *pdts;
                _pggsevFrm->Put(isev, &sevp);
            }

            *pwit = witOld;
            *pdts = dtsOld;
            return (fTrue);
        }
    }

    //
    // 3DMMv1.0: Add pause to event list.
    //
    sev.nfrm = _nfrmCur;
    sev.sevt = sevtPause;
    sevp.wit = *pwit;
    sevp.dts = *pdts;
    if (!_FAddSev(&sev, SIZEOF(int32_t) * 2, &sevp))
    {
        return (fFalse);
    }

    *pwit = witNil;
    *pdts = 0;
    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds/removes a pause to the scene at the current frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *	wit - The pause type, removes pause if wit==witNil.
 *  dts - Valid only if wit==witForTime, dts is in clock ticks.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FPause(WIT wit, int32_t dts)
{
    PSUNP psunp;

    psunp = SUNP::PsunpNew();

    if (psunp == pvNil)
    {
        return (fFalse);
    }

    if (!FPauseCore(&wit, &dts))
    {
        ReleasePpo(&psunp);
        return (fFalse);
    }

    psunp->SetWit(wit);
    psunp->SetDts(dts);
    psunp->SetAdd(fTrue);

    if (!_pmvie->FAddUndo(psunp))
    {
        FPauseCore(&wit, &dts);
        ReleasePpo(&psunp);
        return (fFalse);
    }

    ReleasePpo(&psunp);
    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine sets the background for the scene.
 *
 * Parameters:
 *  ptag - Pointer to the background tag to put on the scene.
 *  ptagOld - Place to store the old background tag.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FSetBkgdCore(PTAG ptag, PTAG ptagOld)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertVarMem(ptagOld);

    SEV sev;
    int32_t isev;
    TAG tag;
    int32_t vlm;
    bool fLoop;
    PMSND pmsnd;
    int32_t sty;

    if (_pbkgd != pvNil)
    {
        //
        // 3DMMv1.0: Replace background
        //

        for (isev = 0; isev < _pggsevStart->IvMac(); isev++)
        {
            _pggsevStart->GetFixed(isev, &sev);

            if (sev.sevt == sevtSetBkgd)
            {
                _pggsevStart->Get(isev, ptagOld);
                if (!_pggsevStart->FPut(isev, SIZEOF(TAG), ptag))
                {
                    return (fFalse);
                }

                _pbkgd->TurnOffLights();
                ReleasePpo(&_pbkgd);

                _MarkMovieDirty();
                if (!_FPlaySev(&sev, ptag, _grfscen))
                {
                    _pggsevStart->FPut(isev, SIZEOF(TAG), ptagOld);
                    _FPlaySev(&sev, ptagOld, _grfscen);
                    return (fFalse);
                }

                AssertPo(_pbkgd, 0);
                _pbkgd->GetName(&_stnName);
                goto LSuccess;
            }
        }

        Bug("No background event found");
        return (fFalse);
    }

    TrashVar(ptagOld);

    //
    // 3DMMv1.0: Add set background event and play it.
    //
    sev.sevt = sevtSetBkgd;

    if (!_pggsevStart->FInsert(0, SIZEOF(TAG), ptag, &sev))
    {
        return (fFalse);
    }

    if (!_FPlaySev(&sev, ptag, _grfscen))
    {
        _pggsevStart->Delete(0);
        return (fFalse);
    }

    AssertPo(_pbkgd, 0);
    _pbkgd->GetName(&_stnName);
    _MarkMovieDirty();

LSuccess:

    while (_pggsevFrm->IvMac() != 0)
    {
        //
        // 3DMMv1.0: Remove stale scene events
        //
        _pggsevFrm->Delete(_pggsevFrm->IvMac() - 1);
    }

    _isevFrmLim = 0;

    // 4DMM defaults new scenes to silence.  The old background "serving
    // suggestion" music is optional per movie through Settings.
    _pbkgd->GetDefaultSound(&tag, &vlm, &fLoop);
    if (_pmvie->FDefaultMusicForNewScenes() && tag.sid != ksidInvalid) // optional new background sound
    {
        // 3DMMv1.0: Note: since FSetBkgdCore is only called at edit time,
        // 3DMMv1.0: it's okay to call FCacheTag.  The background default
        // 3DMMv1.0: sound is not an intrinsic part of the BKGD...it's more
        // 3DMMv1.0: of a "serving suggestion" that the user can remove
        // 3DMMv1.0: once the background is added.
        Assert(!_pmvie->FPlaying(), "Shouldn't cache tags if movie is playing!");
        if (vptagm->FCacheTagToHD(&tag))
        {
            pmsnd = (PMSND)vptagm->PbacoFetch(&tag, MSND::FReadMsnd);
            if (pvNil != pmsnd)
            {
                sty = pmsnd->Sty();
                ReleasePpo(&pmsnd);
                // 3DMMv1.0: non-destructive if we fail
                FAddSndCore(fLoop, fFalse, vlm, sty, 1, &tag);
            }
        }
    }

    if (_pmvie->Pscen() == this)
        _pmvie->RefreshTestLight();
    _pmvie->Pmcc()->SceneChange();

    if (vpcex != pvNil)
    {
        vpcex->EnqueueCid(cidSceneLoaded);
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine sets the background for the scene.
 *
 * Parameters:
 *  pbkgd - Pointer to the background to put on the scene.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FSetBkgd(PTAG ptag)
{
    AssertThis(0);
    AssertVarMem(ptag);

    TAG tagOld;
    PSUNK psunk;
    int32_t icam;

    if (_pbkgd != pvNil)
    {
        icam = _pbkgd->Icam();
    }

    if (!FSetBkgdCore(ptag, &tagOld))
    {
        return (fFalse);
    }

#ifdef DEBUG
    int32_t lw;

    TrashVar(&lw);
    Assert(tagOld.sid != lw, "Use CORE function to set first background");
#endif

    psunk = SUNK::PsunkNew();

    if (psunk == pvNil)
    {
        if (!FSetBkgdCore(&tagOld, ptag))
        {
            _pmvie->ClearUndo();
            PushErc(ercSocNotUndoable);
        }
        return (fFalse);
    }

    psunk->SetTag(&tagOld);
    psunk->SetIcam(icam);
    psunk->SetFBkgd(fTrue);

    if (!_pmvie->FAddUndo(psunk))
    {
        ReleasePpo(&psunk);

        if (!FSetBkgdCore(&tagOld, ptag))
        {
            _pmvie->ClearUndo();
            PushErc(ercSocNotUndoable);
        }
        return (fFalse);
    }

    ReleasePpo(&psunk);

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine returns if a scene is currently empty
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  fTrue if the scene contains only camera changes and sounds
 *    and is only 1 frame long, else fFalse.
 *
 ****************************************************/
bool SCEN::FIsEmpty(void)
{
    AssertThis(0);

    int32_t isev;
    SEV sev;

    if ((_pggsevStart->IvMac() != 1) || ((_nfrmLast - _nfrmFirst) > 0))
    {
        return (fFalse);
    }

    for (isev = 0; isev < _pggsevFrm->IvMac(); isev++)
    {

        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.sevt != sevtChngCamera && sev.sevt != sevtPlaySnd)
        {
            return (fFalse);
        }
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine changes the camera view point at this frame.
 *
 * Parameters:
 *  icam - The camera number in the BKGD to switch to.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FChangeCamCore(int32_t icam, int32_t *picamOld)
{
    AssertThis(0);
    AssertIn(icam, 0, 500);
    AssertVarMem(picamOld);

    PSEV qsev, qsevOld;
    SEV sev;
    int32_t isev, isevCam;

    //
    // 3DMMv1.0: Check for a current camera change.
    //
    *picamOld = 0;

    for (isev = _isevFrmLim - 1; isev >= 0; isev--)
    {
        qsev = (PSEV)_pggsevFrm->QvFixedGet(isev);

        if (qsev->sevt == sevtChngCamera)
        {

            if (qsev->nfrm == _nfrmCur)
            {

                //
                // 3DMMv1.0: Check if this new camera matches the previous
                // 3DMMv1.0: camera
                //
                for (isevCam = isev - 1; isevCam >= 0; isevCam--)
                {

                    qsevOld = (PSEV)_pggsevFrm->QvFixedGet(isevCam);
                    if (qsevOld->sevt == sevtChngCamera)
                    {
                        _pggsevFrm->Get(isevCam, picamOld);
                        break;
                    }
                }

                //
                // 3DMMv1.0: If they are equal, then change camera to
                // 3DMMv1.0: previous camera and remove this change event.
                //
                if (*picamOld == icam)
                {
                    qsevOld = (PSEV)_pggsevFrm->QvFixedGet(isev);
                    _pggsevFrm->Get(isev, picamOld);
                    if (_FPlaySev(qsevOld, &icam, _grfscen))
                    {
                        _pggsevFrm->Delete(isev);
                        _isevFrmLim--;
                        _MarkMovieDirty();
                        goto LSuccess;
                    }
                    return (fFalse);
                }

                //
                // 3DMMv1.0: Change it
                //
                _pggsevFrm->Get(isev, picamOld);
                _pggsevFrm->Put(isev, &icam);
                _MarkMovieDirty();
                if (_FPlaySev(qsev, &icam, _grfscen))
                {
                    goto LSuccess;
                }
                return (fFalse);
            }
            else
            {
                _pggsevFrm->Get(isev, picamOld);
                break;
            }
        }
    }

    if (*picamOld == icam)
    {
        goto LSuccess;
    }

    //
    // 3DMMv1.0: Add camera change to event list.
    //
    sev.nfrm = _nfrmCur;
    sev.sevt = sevtChngCamera;

    if (_FAddSev(&sev, SIZEOF(int32_t), &icam))
    {
        if (_FPlaySev(&sev, &icam, _grfscen))
        {
            goto LSuccess;
        }
        else
        {
            _pggsevFrm->Delete(--_isevFrmLim);
        }
    }

    return (fFalse);

LSuccess:

    //
    // 3DMMv1.0: Check for later camera change
    //
    for (isev = _isevFrmLim; isev < _pggsevFrm->IvMac(); isev++)
    {
        int32_t icamNext;
        qsev = (PSEV)_pggsevFrm->QvFixedGet(isev);
        if (qsev->sevt == sevtChngCamera)
        {
            _pggsevFrm->Get(isev, &icamNext);
            if (icamNext == icam)
            {
                _pggsevFrm->Delete(isev);
                _isevFrmLim;
            }
            break;
        }
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine changes the camera view point at this frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *  icam - The camera number in the BKGD to switch to.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FChangeCam(int32_t icam)
{
    int32_t icamOld;
    PSUNK psunk;
    TAG tagCam;

    if (!vptagm->FBuildChildTag(&_tagBkgd, icam, kctgCam, &tagCam))
    {
        return fFalse;
    }

    if (!vptagm->FCacheTagToHD(&tagCam))
    {
        return fFalse;
    }

    if (!FChangeCamCore(icam, &icamOld))
    {
        return (fFalse);
    }

    psunk = SUNK::PsunkNew();

    if (psunk == pvNil)
    {
        if (!FChangeCamCore(icam, &icamOld))
        {
            _pmvie->ClearUndo();
            PushErc(ercSocNotUndoable);
        }
        return (fFalse);
    }

    psunk->SetIcam(icamOld);
    psunk->SetFBkgd(fFalse);

    if (!_pmvie->FAddUndo(psunk))
    {
        ReleasePpo(&psunk);

        if (!FChangeCamCore(icam, &icamOld))
        {
            _pmvie->ClearUndo();
            PushErc(ercSocNotUndoable);
        }
        return (fFalse);
    }

    ReleasePpo(&psunk);
    return (fTrue);
}

/** 3DMMEx: *************************************************************************
    Deserialize start events from on-disk format
***************************************************************************/
PGG DeserializeStartEVs(int16_t bo, PGG pggsevStart)
{
    AssertPo(pggsevStart, 0);

    int32_t isevStart;
    SEV sev;
    PGG _pggsevStart;
    CHID chid;
    TAGF tagf;
    TAG tag;

    _pggsevStart = pggsevStart->PggDup();
    if (_pggsevStart == pvNil)
        return pvNil;

    for (isevStart = 0; isevStart < _pggsevStart->IvMac(); isevStart++)
    {
        sev = *(SEV *)_pggsevStart->QvFixedGet(isevStart);

        //
        // 3DMMv1.0: Swap byte ordering of entry
        //
        if (bo == kboOther)
        {
            SwapBytesBom((void *)&sev, kbomSev);
            _pggsevStart->PutFixed(isevStart, &sev);
        }

        switch (sev.sevt)
        {
        case sevtAddActr:
            _pggsevStart->Get(isevStart, &chid);
            if (bo == kboOther)
            {
                SwapBytesBom((void *)&chid, kbomLong);
            }
            _pggsevStart->Put(isevStart, &chid);
            break;

        case sevtAddTbox:
            _pggsevStart->Get(isevStart, &chid);
            if (bo == kboOther)
            {
                SwapBytesBom((void *)&chid, kbomLong);
            }
            _pggsevStart->Put(isevStart, &chid);
            break;

        case sevtSetBkgd:
            _pggsevStart->Get(isevStart, &tagf);
            DeserializeTagfToTag(&tagf, &tag);
            _pggsevStart->FPut(isevStart, SIZEOF(TAG), &tag);
            break;

        case sevtChngCamera:
        case sevtPause:
        case sevtPlaySnd:
        default:
            Bug("Bad event in start event list");
            break;
        }
    }

    return _pggsevStart;
}

/** 3DMMEx: *************************************************************************
    Serialize start events to on-disk format
***************************************************************************/
PGG SerializeStartEVs(PGG pggsevStart)
{
    AssertPo(pggsevStart, 0);

    int32_t isevStart;
    SEV sev;
    PGG _pggsevStart;
    TAG tag;
    TAGF tagf;

    _pggsevStart = pggsevStart->PggDup();
    if (_pggsevStart == pvNil)
        return pvNil;

    for (isevStart = 0; isevStart < _pggsevStart->IvMac(); isevStart++)
    {
        sev = *(SEV *)_pggsevStart->QvFixedGet(isevStart);

        switch (sev.sevt)
        {
        case sevtSetBkgd:
            _pggsevStart->Get(isevStart, &tag);
            SerializeTagToTagf(&tag, &tagf);
            _pggsevStart->FPut(isevStart, SIZEOF(TAGF), &tagf);
            break;
        }
    }

    return _pggsevStart;
}

/** 3DMMEx: *************************************************************************
    Deserialize frame events from on-disk format
***************************************************************************/
PGG DeserializeFrameEVs(int16_t bo, PGG pggsevFrm)
{
    AssertPo(pggsevFrm, 0);

    int32_t isevFrm;
    SEV sev;
    PGG _pggsevFrm;
    CHID chid;
    PSSE psseOld;
    PSSE psseNew;
    int32_t ctagc;
    int32_t itagc;
    TAGCF *tagcf;

    _pggsevFrm = pggsevFrm->PggDup();
    if (_pggsevFrm == pvNil)
        return pvNil;

    for (isevFrm = 0; isevFrm < _pggsevFrm->IvMac(); isevFrm++)
    {
        sev = *(SEV *)_pggsevFrm->QvFixedGet(isevFrm);

        //
        // 3DMMv1.0: Swap byte ordering of entry
        //
        if (bo == kboOther)
        {
            SwapBytesBom((void *)&sev, kbomSev);
            _pggsevFrm->PutFixed(isevFrm, &sev);
        }

        switch (sev.sevt)
        {
        case sevtPlaySnd:
            // 3DMMEx: Deserialize GG using TAGCF to SSE with TAGC
            if (!FAllocPv((void **)&psseOld, pggsevFrm->Cb(isevFrm), fmemClear, mprNormal))
                goto LFail;
            _pggsevFrm->Get(isevFrm, psseOld);
            ctagc = psseOld->ctagc;

            psseNew = SSE::PsseNew(ctagc);
            if (psseNew == pvNil)
            {
                FreePpv((void **)&psseOld);
                goto LFail;
            }
            CopyPb(psseOld, psseNew, SIZEOF(SSE));

            tagcf = (TAGCF *)PvAddBv(psseOld, SIZEOF(SSE));
            for (itagc = 0; itagc < ctagc; itagc++)
            {
                TAG tag;

                *(psseNew->Pchid(itagc)) = tagcf[itagc].chid;
                DeserializeTagfToTag(&tagcf[itagc].tagf, &tag);
                *(psseNew->Ptag(itagc)) = tag;
            }
            FreePpv((void **)&psseOld);

            if (bo == kboOther)
            {
                psseNew->SwapBytes();
            }

            if (!_pggsevFrm->FPut(isevFrm, psseNew->Cb(), psseNew))
            {
                ReleasePpsse(&psseNew);
                goto LFail;
            }
            ReleasePpsse(&psseNew);
            break;

        case sevtChngCamera:
            _pggsevFrm->Get(isevFrm, &chid);
            if (bo == kboOther)
            {
                SwapBytesBom((void *)&chid, kbomLong);
            }
            _pggsevFrm->Put(isevFrm, &chid);
            break;
        }
    }

    return _pggsevFrm;

LFail:
    ReleasePpo(&_pggsevFrm);
    return pvNil;
}

/** 3DMMEx: *************************************************************************
    Serialize frame events to on-disk format
***************************************************************************/
PGG SerializeFrameEVs(PGG pggsevFrm)
{
    AssertPo(pggsevFrm, 0);

    int32_t isevFrm;
    SEV sev;
    PGG _pggsevFrm;
    PSSE psseOld;
    PSSE psseNew;
    int32_t ctagc;
    int32_t itagc;
    TAGCF *tagcf;
    int32_t cb;

    _pggsevFrm = pggsevFrm->PggDup();
    if (_pggsevFrm == pvNil)
        return pvNil;

    for (isevFrm = 0; isevFrm < _pggsevFrm->IvMac(); isevFrm++)
    {
        sev = *(SEV *)_pggsevFrm->QvFixedGet(isevFrm);

        switch (sev.sevt)
        {
        case sevtPlaySnd:
            // 3DMMEx: Serialize GG using TAGC to SSE with TAGCF
            if (!FAllocPv((void **)&psseOld, pggsevFrm->Cb(isevFrm), fmemClear, mprNormal))
                return pvNil;
            _pggsevFrm->Get(isevFrm, psseOld);
            ctagc = psseOld->ctagc;

            if (!FAllocPv((void **)&psseNew, pggsevFrm->Cb(isevFrm), fmemClear, mprNormal))
            {
                FreePpv((void **)&psseOld);
                return pvNil;
            }
            CopyPb(psseOld, psseNew, SIZEOF(SSE));

            tagcf = (TAGCF *)PvAddBv(psseNew, SIZEOF(SSE));
            for (itagc = 0; itagc < ctagc; itagc++)
            {
                TAG tag;

                tagcf[itagc].chid = *(psseOld->Pchid(itagc));
                tag = *(psseOld->Ptag(itagc));
                SerializeTagToTagf(&tag, &tagcf[itagc].tagf);
            }

            cb = SIZEOF(SSE) + LwMul(SIZEOF(TAGCF), ctagc);
            _pggsevFrm->FPut(isevFrm, cb, psseNew);

            FreePpv((void **)&psseNew);
            FreePpv((void **)&psseOld);
            break;
        }
    }

    return _pggsevFrm;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine reads in a scene from a chunky file.
 *
 * Parameters:
 *  pcrf - Pointer to the chunky file to read from.
 *  cno  - Cno within the chunky file to read.
 *
 * Returns:
 *  pvNil, if failure, else a pointer to the scene.
 *
 ****************************************************/
SCEN *SCEN::PscenRead(PMVIE pmvie, PCRF pcrf, CNO cno)
{
    AssertPo(pmvie, 0);
    AssertPo(pcrf, 0);

    PSCEN pscen = pvNil;
    BLCK blck;
    KID kid;
    int32_t isevFrm = 0;
    int32_t isevStart = 0;
    SEV sev;
    PSEV qsev;
    int16_t bo;
    PACTR pactr;
    PTBOX ptbox;
    CHID chid;
    SCENH scenh;
    PCFL pcfl;
    PGG pggsevStart;
    PGG pggsevFrm;

    pcfl = pcrf->Pcfl();

    //
    // 3DMMv1.0: Find the chunk and read in the header.
    //
    if (!pcfl->FFind(kctgScen, cno, &blck) || !blck.FUnpackData() || (blck.Cb() != SIZEOF(SCENH)) ||
        !blck.FReadRgb(&scenh, SIZEOF(SCENH), 0))
    {
        goto LFail0;
    }

    //
    // 3DMMv1.0: Check header for byte swapping
    //
    if (scenh.bo == kboOther)
    {
        SwapBytesBom(&scenh, kbomScenh);
    }
    else
    {
        Assert(scenh.bo == kboCur, "Bad Chunky file");
    }

    //
    // 3DMMv1.0: Create our scene object.
    //
    pscen = NewObj SCEN(pmvie);

    if (pscen == pvNil)
    {
        goto LFail0;
    }

    pscen->_isevFrmLim = 0;

    //
    // 3DMMv1.0: Initialize roll call	for actors
    //
    pscen->_pglpactr = GL::PglNew(SIZEOF(PACTR), 0);
    if (pscen->_pglpactr == pvNil)
    {
        goto LFail0;
    }

    pscen->_pglaridSelected = GL::PglNew(SIZEOF(int32_t), 0);
    if (pscen->_pglaridSelected == pvNil)
    {
        goto LFail0;
    }

    //
    // 3DMMv1.0: Initialize roll call	for text boxes
    //
    pscen->_pglptbox = GL::PglNew(SIZEOF(PTBOX), 0);
    if (pscen->_pglptbox == pvNil)
    {
        goto LFail0;
    }

    //
    // 3DMMv1.0: Read Frame information
    //
    pscen->_nfrmLast = scenh.nfrmLast;
    pscen->_nfrmFirst = scenh.nfrmFirst;
    pscen->_trans = scenh.trans;
    pscen->_nfrmCur = pscen->_nfrmFirst - 1;

    //
    // 3DMMv1.0: Read in thumbnail
    //
    if (pcfl->FGetKidChidCtg(kctgScen, cno, 0, kctgThumbMbmp, &kid) && pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
    {
        pscen->_pmbmp = MBMP::PmbmpRead(&blck);
    }

    //
    // 3DMMv1.0: Read in GG of Frame events
    //
    if (!pcfl->FGetKidChidCtg(kctgScen, cno, 0, kctgFrmGg, &kid) || !pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
    {
        goto LFail0;
    }

    pggsevFrm = GG::PggRead(&blck, &bo);
    if (pggsevFrm == pvNil)
    {
        goto LFail0;
    }

    Assert(pggsevFrm->CbFixed() == SIZEOF(SEV), "Bad GG read for event");

    pscen->_pggsevFrm = DeserializeFrameEVs(bo, pggsevFrm);
    ReleasePpo(&pggsevFrm);
    if (pscen->_pggsevFrm == pvNil)
    {
        goto LFail1;
    }

    //
    // 3DMMv1.0: Convert all open tags to pointers.
    //
    for (; isevFrm < pscen->_pggsevFrm->IvMac(); isevFrm++)
    {
        qsev = (PSEV)pscen->_pggsevFrm->QvFixedGet(isevFrm);

        //
        // 3DMMv1.0: Open all tags
        //
        switch (qsev->sevt)
        {
        case sevtPlaySnd:

            PSSE psse;
            int32_t itag;

            psse = SSE::PsseDupFromGg(pscen->_pggsevFrm, isevFrm, fFalse);
            if (pvNil == psse)
                goto LFail1;

            if (bo == kboOther)
            {
                psse->SwapBytes();
            }

            for (itag = 0; itag < psse->ctagc; itag++)
            {
                if (!TAGM::FOpenTag(psse->Ptag(itag), pcrf, pcfl))
                {
                    while (itag-- > 0)
                        TAGM::CloseTag(psse->Ptag(itag));
                    FreePpv((void **)&psse); // 3DMMv1.0: don't ReleasePpsse...tags are already closed
                    goto LFail1;
                }
            }
            // 3DMMv1.0: Put SSE with opened tags back in GG
            pscen->_pggsevFrm->Put(isevFrm, psse);
            FreePpv((void **)&psse); // 3DMMv1.0: don't ReleasePpsse because GG keeps the tags
            break;

        case sevtChngCamera:
        case sevtPause:
        case sevtBlankFrame:
            break;

        case sevtAddActr:
        case sevtSetBkgd:
        case sevtAddTbox:
        default:
            Assert(0, "Bad event in frame event list");
            break;
        }
    }

    //
    // 3DMMv1.0: Read starting events
    //
    ReleasePpo(&pscen->_pggsevStart);

    if (!pcfl->FGetKidChidCtg(kctgScen, cno, 1, kctgStartGg, &kid) || !pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
    {
        goto LFail1;
    }

    pggsevStart = GG::PggRead(&blck, &bo);

    if (pggsevStart == pvNil)
    {
        goto LFail1;
    }

    Assert(pggsevStart->CbFixed() == SIZEOF(SEV), "Bad GG read for event");

    pscen->_pggsevStart = DeserializeStartEVs(bo, pggsevStart);
    ReleasePpo(&pggsevStart);
    if (pscen->_pggsevStart == pvNil)
    {
        goto LFail1;
    }

    //
    // 3DMMv1.0: Convert all open tags to pointers.
    //
    for (; isevStart < pscen->_pggsevStart->IvMac(); isevStart++)
    {
        qsev = (PSEV)pscen->_pggsevStart->QvFixedGet(isevStart);

        //
        // 3DMMv1.0: Convert CHIDs to pointers
        //
        switch (qsev->sevt)
        {
        case sevtAddActr:

            pscen->_pggsevStart->Get(isevStart, &chid);

            if (!pcfl->FGetKidChidCtg(kctgScen, cno, chid, kctgActr, &kid))
            {
                goto LFail1;
            }

            pactr = ACTR::PactrRead(pcrf, kid.cki.cno);
            AssertNilOrPo(pactr, 0);

            if (pactr == pvNil)
            {
                goto LFail1;
            }

            pscen->_pggsevStart->FPut(isevStart, SIZEOF(PACTR), &pactr);
            break;

        case sevtAddTbox:

            pscen->_pggsevStart->Get(isevStart, &chid);

            if (!pcfl->FGetKidChidCtg(kctgScen, cno, chid, kctgTbox, &kid))
            {
                goto LFail1;
            }

            ptbox = TBOX::PtboxRead(pcrf, kid.cki.cno, pscen);
            AssertNilOrPo(ptbox, 0);

            if (ptbox == pvNil)
            {
                goto LFail1;
            }

            pscen->_pggsevStart->FPut(isevStart, SIZEOF(PTBOX), &ptbox);
            break;

        case sevtSetBkgd:
        case sevtChngCamera:
            break;

        case sevtPause:
        case sevtPlaySnd:
        default:
            Bug("Bad event in start event list");
            break;
        }
    }

    //
    // 3DMMv1.0: Read Name
    //
    pcfl->FGetName(kctgScen, cno, &pscen->_stnName);

    AssertPo(pscen, 0);

    return (pscen);

LFail1:

    //
    // 3DMMv1.0: Destroy all created objects
    //
    while (isevStart--)
    {
        pscen->_pggsevStart->GetFixed(isevStart, &sev);
        switch (sev.sevt)
        {
        case sevtAddActr:
            pscen->_pggsevStart->Get(isevStart, &pactr);
            ReleasePpo(&pactr);
            break;
        case sevtAddTbox:
            pscen->_pggsevStart->Get(isevStart, &ptbox);
            ReleasePpo(&ptbox);
            break;

        case sevtChngCamera:
        case sevtSetBkgd:
        case sevtPause:
            break;
        }
    }
    ReleasePpo(&pscen->_pggsevStart);

    //
    // 3DMMv1.0: Destroy all created objects
    //
    while (isevFrm--)
    {
        pscen->_pggsevFrm->GetFixed(isevFrm, &sev);
        switch (sev.sevt)
        {
        case sevtAddActr:
        case sevtAddTbox:
            Bug("Bad event in frame list");
            break;

        case sevtPlaySnd:
            PSSE qsse;
            int32_t itag;

            qsse = (PSSE)pscen->_pggsevFrm->QvGet(isevFrm);
            if (qsse->Cb() != (pscen->_pggsevFrm->CbFixed() + pscen->_pggsevFrm->Cb(isevFrm)))
            {
                Bug("Wrong size for SSE in GG");
                continue;
            }

            /* 3DMMv1.0: Close all tags; retrieve qsse each time...in theory, nothing
                that happens during CloseTag should cause mem to move, but this
                is a failure case, so it's okay to be slow, especially when we
                can be safe-not-sorry.  */
            for (itag = 0; itag < qsse->ctagc; itag++)
            {
                qsse = (PSSE)pscen->_pggsevFrm->QvGet(isevFrm);
                TAGM::CloseTag(qsse->Ptag(itag));
            }
            break;

        case sevtSetBkgd:
        case sevtChngCamera:
        case sevtPause:
        case sevtBlankFrame:
            break;
        }
    }
    ReleasePpo(&pscen->_pggsevFrm);

LFail0:
    ReleasePpo(&pscen);
    return (pvNil);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine plays all the starting events for a scene.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FPlayStartEvents(bool fActorsOnly)
{
    AssertThis(0);

    int32_t isev;

    //
    // 3DMMv1.0: This needs to play all the events in _pggsevStart
    //
    for (isev = 0; isev < _pggsevStart->IvMac(); isev++)
    {
        SEV sev;

        _pggsevStart->GetFixed(isev, &sev);
        if (fActorsOnly && sev.sevt != sevtAddActr)
            continue;

        if (!_FPlaySev(&sev, _pggsevStart->QvGet(isev), _grfscen))
        {
            return (fFalse);
        }
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine returns the bkgd tag in *ptag
 *
 ****************************************************/
bool SCEN::FGetTagBkgd(PTAG ptag)
{
    AssertThis(0);
    AssertVarMem(ptag);

    SEV sev;
    int32_t isevStart;

    for (isevStart = 0; isevStart < _pggsevStart->IvMac(); isevStart++)
    {
        sev = *(PSEV)_pggsevStart->QvFixedGet(isevStart);
        if (sevtSetBkgd == sev.sevt)
        {
            *ptag = *(PTAG)_pggsevStart->QvGet(isevStart);
            return fTrue;
        }
    }
    return fFalse;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine writes a scene into a chunky file.
 *
 * Parameters:
 *  pcrf - Pointer to the chunky file to write to.
 *  pcno  - Cno within the chunky file written to.
 *
 * Returns:
 *  fFalse if it fails, else fTrue.
 *
 ****************************************************/
bool SCEN::FWrite(PCRF pcrf, CNO *pcno)
{
    AssertThis(0);
    AssertPo(pcrf, 0);

    PGG pggFrmTemp = pvNil;
    PGG pggFrm = pvNil;
    PGG pggStartTemp = pvNil;
    PGG pggStart = pvNil;
    SEV sev;
    CHID chidActr, chidTbox;
    CNO cnoChild, cnoFrmEvent, cnoStartEvent;
    SCENH scenh;
    int32_t isevFrm = -1;
    int32_t isevStart = -1;
    int32_t cb;
    BLCK blck;
    PCFL pcfl;
    PCSZ pszFailStage = PszLit("begin");
    int32_t aridWrite = aridNil;
    CNO cnoTmplWrite = cnoNil;

    chidActr = chidTbox = 0;

    pcfl = pcrf->Pcfl();

    MVIE::MultiLog(Pmvie(),
        "scene_write begin scene=%ld frame=%ld first=%ld last=%ld start_events=%ld frame_events=%ld actors=%ld",
        Pmvie() != pvNil ? (long)Pmvie()->Iscen() : -1L, (long)Nfrm(),
        (long)_nfrmFirst, (long)_nfrmLast,
        _pggsevStart != pvNil ? (long)_pggsevStart->IvMac() : -1L,
        _pggsevFrm != pvNil ? (long)_pggsevFrm->IvMac() : -1L,
        _pglpactr != pvNil ? (long)_pglpactr->IvMac() : -1L);

    *pcno = cnoNil;

    //
    // 3DMMv1.0: Get a new CNO for this chunk
    //
    pszFailStage = PszLit("scene_chunk_add");
    if (!pcfl->FAdd(0, kctgScen, pcno))
    {
        goto LFail;
    }

    //
    // 3DMMv1.0: Copy frame event GG to temporary GG
    //
    pszFailStage = PszLit("frame_event_temp_alloc");
    pggFrmTemp = GG::PggNew(SIZEOF(SEV));

    if (pggFrmTemp == pvNil)
    {
        goto LFail;
    }

    for (isevFrm = 0; isevFrm < _pggsevFrm->IvMac(); isevFrm++)
    {
        sev = *(PSEV)_pggsevFrm->QvFixedGet(isevFrm);

        //
        // 3DMMv1.0: Convert pointers in the GG to CHIDs
        //
        switch (sev.sevt)
        {
        case sevtPlaySnd: {
            bool fSuccess = fFalse;
            PSSE psse;
            int32_t itag;
            KID kid;

            psse = SSE::PsseDupFromGg(_pggsevFrm, isevFrm);
            if (pvNil == psse)
            {
                goto LFail;
            }
            for (itag = 0; itag < psse->ctagc; itag++)
            {
                // 3DMMv1.0: For user sounds, the tag's cno must already be correct.
                // 3DMMv1.0: Note: FResolveSndTag can't succeed if the msnd chunk is
                // 3DMMv1.0: not yet a child of the scene.
                if (psse->Ptagc(itag)->tag.sid != ksidUseCrf)
                    continue; // 3DMMv1.0: tag will be closed by ReleasePpsse

                // 3DMMv1.0: If the msnd chunk already exists as this chid of this scene, continue
                if (pcfl->FGetKidChidCtg(kctgScen, *pcno, *psse->Pchid(itag), kctgMsnd, &kid))
                    continue; // 3DMMv1.0: tag will be closed by ReleasePpsse

                // 3DMMv1.0: If the msnd does not exist in this file, it exists in the main movie
                if (!pcfl->FFind(kctgMsnd, psse->Ptag(itag)->cno))
                    continue; // 3DMMv1.0: tag will be closed by ReleasePpsse

                // 3DMMv1.0: The msnd chunk has not been adopted into the scene as the specified chid
                if (!pcfl->FAdoptChild(kctgScen, *pcno, kctgMsnd, psse->Ptag(itag)->cno, *psse->Pchid(itag)))
                {
                    goto LEndPlaySnd;
                }
            }

            if (pggFrmTemp->FInsert(isevFrm, psse->Cb(), psse, &sev))
            {
                fSuccess = fTrue;
            }
        LEndPlaySnd:
            ReleasePpsse(&psse);
            if (!fSuccess)
            {
                goto LFail;
            }
            break;
        }

        case sevtChngCamera:
            if (!pggFrmTemp->FInsert(isevFrm, SIZEOF(int32_t), _pggsevFrm->QvGet(isevFrm), &sev))
            {
                goto LFail;
            }
            break;

        case sevtPause:
            if (!pggFrmTemp->FInsert(isevFrm, SIZEOF(SEVP), _pggsevFrm->QvGet(isevFrm), &sev))
            {
                goto LFail;
            }

            break;

        case sevtBlankFrame:
            if (!pggFrmTemp->FInsert(isevFrm, 0, pvNil, &sev))
                goto LFail;
            break;

        case sevtAddActr:
        case sevtSetBkgd:
        case sevtAddTbox:
            Assert(0, "Bad event in frame event list");
            break;
        }
    }

    //
    // 3DMMv1.0: Copy start event GG to temporary GG
    //
    pszFailStage = PszLit("start_event_temp_alloc");
    pggStartTemp = GG::PggNew(SIZEOF(SEV));

    if (pggStartTemp == pvNil)
    {
        goto LFail;
    }

    for (isevStart = 0; isevStart < _pggsevStart->IvMac(); isevStart++)
    {
        sev = *(PSEV)_pggsevStart->QvFixedGet(isevStart);
        //
        // 3DMMv1.0: Convert pointers in the GG to CHIDs
        //
        switch (sev.sevt)
        {
        case sevtAddActr: {
            PACTR pactrWrite = *(PACTR *)_pggsevStart->QvGet(isevStart);
            TAG tagTmplWrite;
            ClearPb(&tagTmplWrite, SIZEOF(tagTmplWrite));
            aridWrite = pactrWrite != pvNil ? pactrWrite->Arid() : aridNil;
            if (pactrWrite != pvNil)
                pactrWrite->GetTagTmpl(&tagTmplWrite);
            cnoTmplWrite = tagTmplWrite.cno;
            const int32_t fLocalTmplExists =
                pactrWrite != pvNil && tagTmplWrite.sid == ksidUseCrf ?
                    (int)pcfl->FFind(tagTmplWrite.ctg, tagTmplWrite.cno) : -1;
            MVIE::MultiLog(Pmvie(),
                "scene_write actor begin start_event=%ld chid=%ld arid=%ld sid=%ld ctg=0x%08lX tmpl_cno=%ld local_tmpl_exists=%ld onstage=%d",
                (long)isevStart, (long)chidActr, (long)aridWrite,
                (long)tagTmplWrite.sid, (unsigned long)tagTmplWrite.ctg,
                (long)tagTmplWrite.cno, (long)fLocalTmplExists,
                pactrWrite != pvNil ? (int)pactrWrite->FOnStage() : -1);

            pszFailStage = PszLit("actor_chunk_add");
            if (!pcfl->FAddChild(kctgScen, *pcno, chidActr, 0, kctgActr, &cnoChild))
            {
                goto LFail;
            }

            pszFailStage = PszLit("actor_write");
            if (pactrWrite == pvNil || !pactrWrite->FWrite(pcfl, cnoChild, *pcno))
            {
                MVIE::MultiLog(Pmvie(),
                    "scene_write actor FAIL chid=%ld arid=%ld tmpl_cno=%ld actor_cno=%ld el=%ld",
                    (long)chidActr, (long)aridWrite, (long)cnoTmplWrite,
                    (long)cnoChild, (long)pcfl->ElError());
                goto LFail;
            }

            pszFailStage = PszLit("actor_start_event_insert");
            if (!pggStartTemp->FInsert(isevStart, SIZEOF(CHID), &chidActr, &sev))
            {
                goto LFail;
            }

            MVIE::MultiLog(Pmvie(),
                "scene_write actor ok chid=%ld arid=%ld tmpl_cno=%ld actor_cno=%ld",
                (long)chidActr, (long)aridWrite, (long)cnoTmplWrite, (long)cnoChild);
            chidActr++;
            aridWrite = aridNil;
            cnoTmplWrite = cnoNil;
            break;
        }

        case sevtSetBkgd:
            if (!TAGM::FSaveTag((PTAG)_pggsevStart->QvGet(isevStart), pcrf, fFalse))
            {
                goto LFail;
            }

            if (!pggStartTemp->FInsert(isevStart, SIZEOF(TAG), _pggsevStart->QvGet(isevStart), &sev))
            {
                goto LFail;
            }
            break;

        case sevtChngCamera:
            if (!pggStartTemp->FInsert(isevStart, SIZEOF(int32_t), _pggsevStart->QvGet(isevStart), &sev))
            {
                goto LFail;
            }
            break;

        case sevtAddTbox:
            if (!pcfl->FAddChild(kctgScen, *pcno, chidTbox, 0, kctgTbox, &cnoChild))
            {
                goto LFail;
            }

            if (!(*(PTBOX *)_pggsevStart->QvGet(isevStart))->FWrite(pcfl, cnoChild))
            {
                goto LFail;
            }

            if (!pggStartTemp->FInsert(isevStart, SIZEOF(CHID), &chidTbox, &sev))
            {
                goto LFail;
            }

            chidTbox++;
            break;

        case sevtPause:
        case sevtPlaySnd:
        default:
            Assert(0, "Bad event in frame event list");
            break;
        }
    }

    //
    // 3DMMv1.0: Save info into scene chunk
    //
    pszFailStage = PszLit("serialize_frame_events");
    pggFrm = SerializeFrameEVs(pggFrmTemp);
    if (pggFrm == pvNil)
    {
        goto LFail;
    }

    cb = pggFrm->CbOnFile();
    pszFailStage = PszLit("frame_event_chunk_add");
    if (!pcfl->FAdd(cb, kctgFrmGg, &cnoFrmEvent, &blck))
    {
        goto LFail;
    }

    pszFailStage = PszLit("frame_event_write");
    if (!pggFrm->FWrite(&blck))
    {
        pcfl->Delete(kctgFrmGg, cnoFrmEvent);
        goto LFail;
    }
    ReleasePpo(&pggFrm);

    pszFailStage = PszLit("frame_event_adopt");
    if (!pcfl->FAdoptChild(kctgScen, *pcno, kctgFrmGg, cnoFrmEvent, 0))
    {
        pcfl->Delete(kctgFrmGg, cnoFrmEvent);
        goto LFail;
    }
    pcfl->SetLoner(kctgFrmGg, cnoFrmEvent, fFalse);

    pszFailStage = PszLit("serialize_start_events");
    pggStart = SerializeStartEVs(pggStartTemp);
    if (pggStart == pvNil)
    {
        goto LFail;
    }

    cb = pggStart->CbOnFile();
    pszFailStage = PszLit("start_event_chunk_add");
    if (!pcfl->FAdd(cb, kctgStartGg, &cnoStartEvent, &blck))
    {
        ReleasePpo(&pggStart);
        goto LFail;
    }

    pszFailStage = PszLit("start_event_write");
    if (!pggStart->FWrite(&blck))
    {
        pcfl->Delete(kctgStartGg, cnoStartEvent);
        ReleasePpo(&pggStart);
        goto LFail;
    }
    ReleasePpo(&pggStart);

    pszFailStage = PszLit("start_event_adopt");
    if (!pcfl->FAdoptChild(kctgScen, *pcno, kctgStartGg, cnoStartEvent, 1))
    {
        pcfl->Delete(kctgStartGg, cnoStartEvent);
        goto LFail;
    }
    pcfl->SetLoner(kctgStartGg, cnoStartEvent, fFalse);

    //
    // 3DMMv1.0: Save thumbnail, if there is one.
    //
    _UpdateThumbnail();

    if (_pmbmp != pvNil)
    {
        cb = _pmbmp->CbOnFile();
        pszFailStage = PszLit("thumbnail_chunk_add");
        if (!pcfl->FAdd(cb, kctgThumbMbmp, &cnoChild, &blck))
        {
            goto LFail;
        }

        pszFailStage = PszLit("thumbnail_write");
        if (!_pmbmp->FWrite(&blck))
        {
            pcfl->Delete(kctgThumbMbmp, cnoChild);
            goto LFail;
        }

        pszFailStage = PszLit("thumbnail_adopt");
        if (!pcfl->FAdoptChild(kctgScen, *pcno, kctgThumbMbmp, cnoChild, 0))
        {
            pcfl->Delete(kctgThumbMbmp, cnoChild);
            goto LFail;
        }
        pcfl->SetLoner(kctgThumbMbmp, cnoChild, fFalse);
    }

    //
    // 3DMMv1.0: Create header buffer for scene chunk
    //
    scenh.bo = kboCur;
    scenh.osk = koskCur;
    scenh.nfrmLast = _nfrmLast;
    scenh.nfrmFirst = _nfrmFirst;
    scenh.trans = _trans;

    //
    // 3DMMv1.0: Write scene chunk
    //
    pszFailStage = PszLit("scene_set_name");
    if (!pcfl->FSetName(kctgScen, *pcno, &_stnName))
    {
        goto LFail;
    }

    pszFailStage = PszLit("scene_header_write");
    if (!pcfl->FPutPv((void *)&scenh, SIZEOF(SCENH), kctgScen, *pcno))
    {
        goto LFail;
    }

    ReleasePpo(&pggFrmTemp);
    ReleasePpo(&pggStartTemp);

    MVIE::MultiLog(Pmvie(),
        "scene_write ok scene=%ld cno=%ld actors=%ld tboxes=%ld",
        Pmvie() != pvNil ? (long)Pmvie()->Iscen() : -1L,
        (long)*pcno, (long)chidActr, (long)chidTbox);
    return (fTrue);

LFail:
    MVIE::MultiLog(Pmvie(),
        "scene_write FAIL stage=%s scene=%ld frame=%ld cno=%ld isev_frame=%ld isev_start=%ld chid_actor=%ld arid=%ld tmpl_cno=%ld el=%ld",
        pszFailStage != pvNil ? pszFailStage : "unknown",
        Pmvie() != pvNil ? (long)Pmvie()->Iscen() : -1L, (long)Nfrm(),
        (long)*pcno, (long)isevFrm, (long)isevStart, (long)chidActr,
        (long)aridWrite, (long)cnoTmplWrite, (long)pcfl->ElError());

    //
    // 3DMMv1.0: Delete chunks createdk.
    //
    if (*pcno != cnoNil)
    {
        pcfl->Delete(kctgScen, *pcno);
    }

    ReleasePpo(&pggStartTemp);
    ReleasePpo(&pggFrmTemp);

    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine resolves all sound tags
 *
 * Parameters:
 *  None
 * Returns:
 *  None
 *
 ****************************************************/
bool SCEN::FResolveAllSndTags(CNO cnoScen)
{
    AssertThis(0);

    int32_t ipactr, ipactrMac;
    int32_t isev, isevMac;
    bool fSuccess = fFalse;

    ipactrMac = _pglpactr->IvMac();
    for (ipactr = 0; ipactr < ipactrMac; ipactr++)
    {
        PACTR pactr;

        _pglpactr->Get(ipactr, &pactr);
        if (!pactr->FResolveAllSndTags(cnoScen))
            goto LFail;
    }

    _pggsevFrm->Lock();
    isevMac = _pggsevFrm->IvMac();
    for (isev = 0; isev < isevMac; isev++)
    {
        int32_t itag;
        PSSE psse;
        SEV sev;

        sev = *(PSEV)_pggsevFrm->QvFixedGet(isev);
        if (sev.sevt != sevtPlaySnd)
            continue;

        psse = (PSSE)_pggsevFrm->QvGet(isev);
        for (itag = 0; itag < psse->ctagc; itag++)
        {
            if (psse->Ptag(itag)->sid == ksidUseCrf)
            {
                if (!_pmvie->FResolveSndTag(psse->Ptag(itag), *psse->Pchid(itag), cnoScen))
                {
                    goto LFail;
                }
            }
        }
    }
    _pggsevFrm->Unlock();

    fSuccess = fTrue;
LFail:
    return fSuccess;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine removes all actors from movie's roll call.
 *
 * Parameters:
 *  fDelIfOnlyRef  --  fTrue indicates to remove the roll-call entry
 *        if a given actr is only referenced by this scene.
 *
 * Returns:
 *  None
 *
 ****************************************************/
void SCEN::RemActrsFromRollCall(bool fDelIfOnlyRef)
{
    AssertThis(0);

    PACTR pactr;
    int32_t ipactr;

    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        Pmvie()->RemFromRollCall(pactr, fDelIfOnlyRef);
    }
}

/** 3DMMv1.0: **************************************************
 *
 * This routine adds all actors to movie's roll call.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FAddActrsToRollCall(void)
{
    AssertThis(0);

    PACTR pactr;
    int32_t ipactr;
    STN stn;

    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {

        _pglpactr->Get(ipactr, &pactr);
        AssertPo(pactr, 0);

        // A stock Actor/Prop that has been promoted by Actor Studio remains
        // stock in older serialized scene ACTR chunks.  Resolve that logical
        // stock identity to the movie-local replacement as each scene is
        // brought back into memory, before FAddToRollCall() can overwrite the
        // movie roll-call entry with the stale stock tag.
        TAG tagSource;
        TAG tagReplacement;
        pactr->GetTagTmpl(&tagSource);
        if (Pmvie()->FResolve4DMMReplacementTemplateTag(&tagSource, &tagReplacement))
        {
            if (Pmvie()->FOpen4DMMOwnedTemplateTag(tagReplacement.cno, &tagReplacement))
            {
                if (pactr->FChangeTagTmpl(&tagReplacement))
                {
                    MVIE::MultiLog(Pmvie(),
                        "actor_studio_template_scene_resolve arid=%ld source_sid=%ld source_ctg=%lu source_cno=%ld replacement=%ld",
                        (long)pactr->Arid(), (long)tagSource.sid, (unsigned long)tagSource.ctg,
                        (long)tagSource.cno, (long)tagReplacement.cno);
                }
                TAGM::CloseTag(&tagReplacement);
            }
        }

        pactr->GetName(&stn);

        if (!Pmvie()->FAddToRollCall(pactr, &stn))
        {

            for (; ipactr > 0;)
            {
                ipactr--;
                _pglpactr->Get(ipactr, &pactr);
                Pmvie()->RemFromRollCall(pactr);
            }

            return (fFalse);
        }
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine returns the scene's thumbnail.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  None
 *
 ****************************************************/
PMBMP SCEN::PmbmpThumbnail(void)
{
    AssertThis(0);

    _UpdateThumbnail();
    return (_pmbmp);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine updates the mbmp associated with the
 * thumbnail for the scene.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  None
 *
 ****************************************************/
void SCEN::_UpdateThumbnail(void)
{
    AssertThis(0);

    int32_t nfrmCur;
    PGPT pgpt, pgptThumb;
    PMVU pmvu;
    PTBOX ptbox = PtboxSelected();
    PACTR pactr = PactrSelected();
    RC rc, rcThumb;
    int32_t grfscenSave;
    int32_t dtimSnd;

    dtimSnd = Pmvie()->Pmsq()->DtimSnd();
    Pmvie()->Pmsq()->SndOff();

    pmvu = (PMVU)Pmvie()->PddgGet(0);
    if ((_pbkgd == pvNil) || (pmvu == pvNil))
    {
        goto LEnd;
    }
    AssertPo(pmvu, 0);

    if (!Pmvie()->FDirty())
    {
        goto LEnd;
    }

    rc.Set(0, 0, Pmvie()->Pmcc()->Dxp(), Pmvie()->Pmcc()->Dyp());
    pgpt = GPT::PgptNewOffscreen(&rc, 8);

    if (pgpt == pvNil)
    {
        goto LEnd;
    }

    AssertPo(pgpt, 0);

    rcThumb.Set(0, 0, kdxpThumbnail, kdypThumbnail);
    pgptThumb = GPT::PgptNewOffscreen(&rcThumb, 8);

    if (pgptThumb == pvNil)
    {
        ReleasePpo(&pgpt);
        goto LEnd;
    }

    AssertPo(pgptThumb, 0);

    pgptThumb->SetOffscreenColors(Pmvie()->PglclrThumbPalette());

    grfscenSave = _grfscen;
    Disable(fscenPauses | fscenSounds);

    nfrmCur = _nfrmCur;
    if ((_nfrmCur != _nfrmFirst) && !FGotoFrm(_nfrmFirst))
    {
        ReleasePpo(&pgpt);
        ReleasePpo(&pgptThumb);
        _grfscen = grfscenSave;
        goto LEnd;
    }

    if (pmvu->FTextMode())
    {
        SelectTbox(pvNil);
    }
    else
    {
        SelectActr(pvNil);
    }
    pmvu->DrawTree(pgpt, pvNil, &rc, fgobNoVis);
    if (pmvu->FTextMode())
    {
        SelectTbox(ptbox);
    }
    else
    {
        SelectActr(pactr);
    }

    BLOCK
    {
        GNV gnv(pgpt);
        GNV gnvThumb(pgptThumb);
        gnvThumb.CopyPixels(&gnv, &rc, &rcThumb);

        ReleasePpo(&_pmbmp);

        _pmbmp = MBMP::PmbmpNew(pgptThumb->PrgbLockPixels(), pgptThumb->CbRow(), kdypThumbnail, &rcThumb, 0, 0,
                                kbTransparent);
        pgptThumb->Unlock();

        ReleasePpo(&pgpt);
        ReleasePpo(&pgptThumb);
        if ((nfrmCur > _nfrmFirst) && (nfrmCur <= _nfrmLast))
        {
            FGotoFrm(nfrmCur);
        }

        _grfscen = grfscenSave;
    }

LEnd:

    Pmvie()->Pmsq()->SndOnDtim(dtimSnd);
    return;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine marks the movie as dirty.
 *
 * Parameters:
 *  fDirty - Set dirty or not.
 *
 * Returns:
 *  None
 *
 ****************************************************/
void SCEN::MarkDirty(bool fDirty)
{
    AssertThis(0);
    if (fDirty)
    {
        _MarkMovieDirty();
    }
}

/** 3DMMv1.0: **************************************************
 *
 * This routine pastes an actor into the current scene
 *
 * Parameters:
 *  pactr - Pointer to the actor to paste
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FPasteActrCore(PACTR pactr, bool fInPlace)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    //
    // 3DMMv1.0: Paste the actor.
    //
    if (!pactr->FPaste(_nfrmCur, this, fInPlace))
    {
        return (fFalse);
    }

    //
    // 3DMMv1.0: Add the actor
    //
    if (!FAddActrCore(pactr))
    {
        return (fFalse);
    }

    InvalFrmRange();
    if (!pactr->FGotoFrame(_nfrmCur))
    {
        RemActrCore(pactr->Arid());
        return (fFalse);
    }
    _MarkMovieDirty();

    _pmvie->Pmcc()->UpdateRollCall();
    UpdateSndFrame();

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine pastes an actor into the current scene
 * and creates an undo object for the action.
 *
 * NOTE: This does not need an undo object, since the
 * tool is getting set to a "place" tool, which will
 * create the appropriate undo type.
 *
 * Parameters:
 *  pactr - Pointer to the actor to paste
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::FPasteActr(PACTR pactr, bool fInPlace)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    if (!FPasteActrCore(pactr, fInPlace))
    {
        return (fFalse);
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine makes all actors go to a specific frame.
 *
 * Parameters:
 *  nfrm - Frame number to go to.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::_FForceActorsToFrm(int32_t nfrm, bool *pfSoundInFrame)
{
    AssertThis(0);
    AssertIn(nfrm, klwMin, klwMax);

    PACTR pactr;
    int32_t iactr;

    for (iactr = 0; iactr < _pglpactr->IvMac(); iactr++)
    {
        _pglpactr->Get(iactr, &pactr);
        AssertPo(pactr, 0);
        MVIE::DiagLog("SCEN::_FForceActorsToFrm scene=%ld frame=%ld actor_index=%ld actor=%p arid=%ld before FGotoFrame",
                      (long)Pmvie()->Iscen(), (long)nfrm, (long)iactr, pactr, (long)pactr->Arid());

        if (!pactr->FGotoFrame(nfrm, pfSoundInFrame))
        {
            MVIE::DiagLog("SCEN::_FForceActorsToFrm FAILED scene=%ld frame=%ld actor_index=%ld actor=%p arid=%ld",
                          (long)Pmvie()->Iscen(), (long)nfrm, (long)iactr, pactr, (long)pactr->Arid());
            return (fFalse);
        }
        MVIE::DiagLog("SCEN::_FForceActorsToFrm scene=%ld frame=%ld actor_index=%ld actor=%p arid=%ld after FGotoFrame",
                      (long)Pmvie()->Iscen(), (long)nfrm, (long)iactr, pactr, (long)pactr->Arid());
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine makes all text boxes go to a specific frame.
 *
 * Parameters:
 *  nfrm - Frame number to go to.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SCEN::_FForceTboxesToFrm(int32_t nfrm)
{
    AssertThis(0);
    AssertIn(nfrm, klwMin, klwMax);

    PTBOX ptbox;
    int32_t itbox;

    for (itbox = 0; itbox < _pglptbox->IvMac(); itbox++)
    {
        _pglptbox->Get(itbox, &ptbox);
        AssertPo(ptbox, 0);

        if (!ptbox->FGotoFrame(nfrm))
        {
            return (fFalse);
        }
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * Hides all text boxes in this scene
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SCEN::HideTboxes(void)
{
    AssertThis(0);

    PTBOX ptbox;
    int32_t iptbox;
    int32_t nfrmStart, nfrmLast;

    for (iptbox = 0; iptbox < _pglptbox->IvMac(); iptbox++)
    {
        _pglptbox->Get(iptbox, &ptbox);
        AssertPo(ptbox, 0);
        if (ptbox->FGetLifetime(&nfrmStart, &nfrmLast))
        {
            AssertDo(ptbox->FGotoFrame(nfrmStart - 1), "Could not remove a text box");
        }
    }
}

/** 3DMMv1.0: **************************************************
 *
 * Hides all actors in this scene
 *
 * Parameters:
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SCEN::HideActors(void)
{
    AssertThis(0);

    PACTR pactr;
    int32_t ipactr;

    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        AssertPo(pactr, 0);
        pactr->Hide();
    }
}

/** 3DMMv1.0: **************************************************
 *
 * Shows all actors in this scene
 *
 * Parameters:
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SCEN::ShowActors(void)
{
    AssertThis(0);

    PACTR pactr;
    int32_t ipactr;

    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        AssertPo(pactr, 0);
        pactr->Show();
    }
}

/** 3DMMv1.0: **************************************************
 *
 * This routine marks the movie dirty.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SCEN::_MarkMovieDirty()
{
    AssertThis(0);

    if (_pmvie != pvNil)
    {
        _pmvie->SetDirty();
    }
}

/** 3DMMv1.0: **************************************************
 *
 * This routine sets the scene to the given movie
 *
 * Parameters:
 *  pmvie - The new parent movie
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SCEN::SetMvie(PMVIE pmvie)
{
    AssertThis(0);
    AssertPo(pmvie, 0);

    _pmvie = pmvie;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine reads from a chunky file and creates
 * a list of all the tags in the scene.
 *
 * Parameters:
 *  pcfl - Pointer to the chunky file to read from.
 *  cno  - Cno within the chunky file to read.
 *  ptagl - tag list to put tags in
 *
 * Returns:
 *  fFalse if an error occurred, else fTrue
 *
 ****************************************************/
bool SCEN::FAddTagsToTagl(PCFL pcfl, CNO cno, PTAGL ptagl)
{
    AssertPo(pcfl, 0);
    AssertPo(ptagl, 0);

    BLCK blck;
    KID kid;
    int32_t isev;
    PSEV qsev;
    int16_t bo;
    PGG pggsev;
    PGG pggsevStart;
    PGG pggsevFrm;
    TAG tag;
    TAG tagBkgd;
    PGL pgltagSrc;
    TAG tagSrc;
    int32_t itagSrc;
    CHID chid;

    tagBkgd.sid = ksidInvalid;

    //
    // 3DMMv1.0: Read starting events
    //
    if (!pcfl->FGetKidChidCtg(kctgScen, cno, 1, kctgStartGg, &kid))
    {
        return fFalse;
    }

    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
    {
        return fFalse;
    }

    pggsev = GG::PggRead(&blck, &bo);

    if (pggsev == pvNil)
    {
        return fFalse;
    }

    Assert(pggsev->CbFixed() == SIZEOF(SEV), "Bad GG read for event");

    pggsevStart = DeserializeStartEVs(bo, pggsev);
    ReleasePpo(&pggsev);
    if (pggsevStart == pvNil)
    {
        return fFalse;
    }

    //
    // 3DMMv1.0: Find all tags in starting events
    //
    for (isev = 0; isev < pggsevStart->IvMac(); isev++)
    {
        qsev = (PSEV)pggsevStart->QvFixedGet(isev);

        //
        // 3DMMv1.0: Find appropriate event types
        //
        switch (qsev->sevt)
        {
        case sevtAddActr:

            pggsevStart->Get(isev, &chid);

            if (!pcfl->FGetKidChidCtg(kctgScen, cno, chid, kctgActr, &kid))
            {
                ReleasePpo(&pggsevStart);
                return fFalse;
            }
            else
            {
                bool fError;
                pgltagSrc = ACTR::PgltagFetch(pcfl, kid.cki.cno, &fError);

                if (fError)
                {
                    ReleasePpo(&pgltagSrc);
                    ReleasePpo(&pggsevStart);
                    return fFalse;
                }

                if (pgltagSrc != pvNil)
                {

                    for (itagSrc = 0; itagSrc < pgltagSrc->IvMac(); itagSrc++)
                    {
                        pgltagSrc->Get(itagSrc, &tagSrc);
                        if (!ptagl->FInsertTag(&tagSrc))
                        {
                            ReleasePpo(&pgltagSrc);
                            ReleasePpo(&pggsevStart);
                            return fFalse;
                        }
                    }

                    ReleasePpo(&pgltagSrc);
                }
            }
            break;

        case sevtSetBkgd:
            pggsevStart->Get(isev, &tag);

            if (!BKGD::FAddTagsToTagl(&tag, ptagl))
            {
                ReleasePpo(&pggsevStart);
                return fFalse;
            }
            tagBkgd = tag;

            break;

        case sevtAddTbox:
            break;

        case sevtPlaySnd:
        case sevtChngCamera:
        case sevtPause:
        default:
            Bug("Bad event in start event list");
            break;
        }
    }

    ReleasePpo(&pggsevStart);

    //
    // 3DMMv1.0: Read in GG of Frame events
    //
    if (!pcfl->FGetKidChidCtg(kctgScen, cno, 0, kctgFrmGg, &kid))
    {
        return fFalse;
    }

    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
    {
        return fFalse;
    }

    pggsev = GG::PggRead(&blck, &bo);

    if (pggsev == pvNil)
    {
        return fFalse;
    }

    Assert(pggsev->CbFixed() == SIZEOF(SEV), "Bad GG read for event");

    pggsevFrm = DeserializeFrameEVs(bo, pggsev);
    ReleasePpo(&pggsev);
    if (pggsevFrm == pvNil)
    {
        return fFalse;
    }

    //
    // 3DMMv1.0: Look in all events for tags
    //
    for (isev = 0; isev < pggsevFrm->IvMac(); isev++)
    {
        qsev = (PSEV)pggsevFrm->QvFixedGet(isev);

        //
        // 3DMMv1.0: Find appropriate event types
        //
        switch (qsev->sevt)
        {
        case sevtChngCamera:
            pggsevFrm->Get(isev, &chid);
            if (tagBkgd.sid == ksidInvalid)
            {
                Bug("no background event!?");
                ReleasePpo(&pggsevFrm);
                return fFalse;
            }
            if (!ptagl->FInsertChild(&tagBkgd, chid, kctgCam))
            {
                ReleasePpo(&pggsevFrm);
                return fFalse;
            }
            break;

        case sevtPause:
        case sevtBlankFrame:
            break;

        case sevtPlaySnd:

            PSSE psse;
            int32_t itag;

            psse = SSE::PsseDupFromGg(pggsevFrm, isev, fFalse);
            if (pvNil == psse)
            {
                ReleasePpo(&pggsevFrm);
                return fFalse;
            }
            if (bo == kboOther)
            {
                psse->SwapBytes();
            }

            //
            // 3DMMv1.0: Insert the tags in order
            //
            for (itag = 0; itag < psse->ctagc; itag++)
            {
                if (!ptagl->FInsertTag(psse->Ptag(itag)))
                {
                    ReleasePpsse(&psse);
                    ReleasePpo(&pggsevFrm);
                    return fFalse;
                }
            }
            ReleasePpsse(&psse);
            break;

        case sevtAddActr:
        case sevtSetBkgd:
        case sevtAddTbox:
        default:
            Bug("Bad event in frame event list");
            break;
        }
    }

    ReleasePpo(&pggsevFrm);

    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * This returns the actor in the current scene with
 * the given arid.
 *
 * Parameters:
 *   Arid to look for.
 *
 * Returns:
 *   pvNil if failure, else the actor.
 *
 ****************************************************/
PACTR SCEN::PactrFromArid(int32_t arid)
{
    AssertThis(0);
    Assert(arid != aridNil, "Bad long");

    int32_t iactr;
    PACTR pactr;

    //
    // 3DMMv1.0: Search current scene for the actor.
    //
    for (iactr = 0; iactr < _pglpactr->IvMac(); iactr++)
    {
        _pglpactr->Get(iactr, &pactr);

        if (pactr->Arid() == arid)
        {
            return (pactr);
        }
    }

    return (pvNil);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine chops off the rest of the scene.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool SCEN::FChopCore()
{
    AssertThis(0);

    PTBOX ptbox;
    PACTR pactr;
    bool fAlive;
    int32_t ipo;
    int32_t nfrmStart, nfrmLast;

    if (_nfrmCur == _nfrmLast)
    {
        return (fTrue);
    }

    //
    // 3DMMv1.0: Chop all actors off
    //
    for (ipo = 0; ipo < _pglpactr->IvMac();)
    {
        _pglpactr->Get(ipo, &pactr);
        AssertPo(pactr, 0);
        pactr->DeleteFwdCore(fTrue, &fAlive);

        if (!fAlive)
        {
            RemActrCore(pactr->Arid());
        }
        else
        {
            ipo++;
        }
    }

    //
    // 3DMMv1.0: Chop all text boxes off
    //
    for (ipo = 0; ipo < _pglptbox->IvMac();)
    {
        _pglptbox->Get(ipo, &ptbox);
        AssertPo(ptbox, 0);

        if (ptbox->FGetLifetime(&nfrmStart, &nfrmLast) && (nfrmStart > _nfrmCur))
        {
            //
            // 3DMMv1.0: Ok if this fails.
            //
            FRemTboxCore(ptbox);
        }
        else
        {
            if (nfrmLast >= _nfrmCur)
            {

                //
                // 3DMMv1.0: Here we extend the lifetime of the text box to
                // 3DMMv1.0: infinite, cuz we removed the frame with the
                // 3DMMv1.0: Hide().
                //
                if (ptbox->FGotoFrame(klwMax - 1))
                {
                    ptbox->FShowCore();
                    ptbox->FGotoFrame(_nfrmCur);
                }
            }

            ipo++;
        }
    }

    //
    // 3DMMv1.0: Remove all scene events from here forward
    //
    for (; _isevFrmLim < _pggsevFrm->IvMac();)
    {
        _pggsevFrm->Delete(_isevFrmLim);
    }

    //
    // 3DMMv1.0: Set new limit
    //
    if (_nfrmLast != _nfrmCur)
    {
        // .3ct frame numbers are one-based within the visible scene.  Remove
        // camera samples beyond the new end and, when a tween is cut in the
        // middle, synthesize the mathematically equivalent endpoint here.
        Pmvie()->TrimCameraTrackAfter(Pmvie()->Iscen(), _nfrmCur - _nfrmFirst + 1);
        _nfrmLast = _nfrmCur;
        Pmvie()->SetDirty();
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine chops off the rest of the scene and
 * creates an undo object as well.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool SCEN::FChop()
{
    MVIE::MultiLog(Pmvie(),
        "scene_chop_after begin scene=%ld current=%ld first=%ld last=%ld camera_input=%d manual_frame=%d",
        (long)Pmvie()->Iscen(), (long)_nfrmCur, (long)_nfrmFirst, (long)_nfrmLast,
        (int)Pmvie()->FCameraInputActive(), (int)Pmvie()->FManualCameraFrameActive());

    if (_nfrmCur == _nfrmLast)
    {
        MVIE::MultiLog(Pmvie(), "scene_chop_after noop already_last scene=%ld frame=%ld",
                       (long)Pmvie()->Iscen(), (long)_nfrmCur);
        return (fTrue);
    }

    if (!FAddSnapshotUndo(PszLit("Delete Everything After")))
    {
        MVIE::MultiLog(Pmvie(), "scene_chop_after FAILED stage=undo scene=%ld frame=%ld",
                       (long)Pmvie()->Iscen(), (long)_nfrmCur);
        return (fFalse);
    }

    const int32_t nfrmCutVisible = _nfrmCur - _nfrmFirst + 1;
    if (!FChopCore())
    {
        Pmvie()->ClearUndo();
        MVIE::MultiLog(Pmvie(), "scene_chop_after FAILED stage=core scene=%ld cut_visible=%ld",
                       (long)Pmvie()->Iscen(), (long)nfrmCutVisible);
        return (fFalse);
    }

    // Scene bounds and the .3ct camera sidecar changed together. Refresh the
    // camera, timeline controls, and movie view immediately after the armed
    // Scene-tab tool receives its viewport click.
    Pmvie()->ApplyCameraTrack();
    Pmvie()->MarkViews();
    Pmvie()->Pmcc()->UpdateScrollbars();
    Pmvie()->InvalViewsAndScb();

    MVIE::MultiLog(Pmvie(),
        "scene_chop_after ok scene=%ld cut_visible=%ld new_current=%ld new_first=%ld new_last=%ld",
        (long)Pmvie()->Iscen(), (long)nfrmCutVisible, (long)_nfrmCur,
        (long)_nfrmFirst, (long)_nfrmLast);
    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine chops off the rest of the scene, backwards
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool SCEN::FChopBackCore()
{
    AssertThis(0);

    PTBOX ptbox;
    PACTR pactr;
    SEV sev;
    bool fAlive;
    bool fCopyCam;
    int32_t ipo;
    int32_t nfrmStart, nfrmLast;

    if (_nfrmCur == _nfrmFirst)
    {
        return (fTrue);
    }

    //
    // 3DMMv1.0: Chop all actors off
    //
    for (ipo = 0; ipo < _pglpactr->IvMac();)
    {
        _pglpactr->Get(ipo, &pactr);
        AssertPo(pactr, 0);
        pactr->DeleteBackCore(&fAlive);

        if (!fAlive)
        {
            RemActrCore(pactr->Arid());
        }
        else
        {
            ipo++;
        }
    }

    //
    // 3DMMv1.0: Chop all text boxes off
    //
    for (ipo = 0; ipo < _pglptbox->IvMac();)
    {
        _pglptbox->Get(ipo, &ptbox);
        AssertPo(ptbox, 0);

        if (ptbox->FGetLifetime(&nfrmStart, &nfrmLast) && (nfrmLast < _nfrmCur))
        {
            //
            // 3DMMv1.0: Ok if this fails.
            //
            FRemTboxCore(ptbox);
        }
        else
        {
            if (nfrmStart < _nfrmCur)
            {
                Assert(ptbox->FIsVisible(), "Bad tbox");
                ptbox->SetStartFrame(_nfrmCur);
            }

            ipo++;
        }
    }

    //
    // 3DMMv1.0: Remove all scene events from here backward, except camera
    // 3DMMv1.0: changes, keep the most recent camera change.
    //
    fCopyCam = fTrue;
    for (; _isevFrmLim > 0;)
    {
        _isevFrmLim--;

        _pggsevFrm->GetFixed(_isevFrmLim, &sev);

        if ((sev.sevt == sevtChngCamera) && (fCopyCam))
        {

            fCopyCam = fFalse;
            sev.nfrm = _nfrmCur;
            _pggsevFrm->PutFixed(_isevFrmLim, &sev);
        }
        else
        {

            if (sev.nfrm == _nfrmCur)
            {
                continue;
            }

            _pggsevFrm->Delete(_isevFrmLim);
        }
    }

    _isevFrmLim = 0;
    for (; _isevFrmLim < _pggsevFrm->IvMac(); _isevFrmLim++)
    {

        _pggsevFrm->GetFixed(_isevFrmLim, &sev);
        if (sev.nfrm != _nfrmCur)
        {
            break;
        }
    }

    //
    // 3DMMv1.0: Set new limit
    //
    if (_nfrmFirst != _nfrmCur)
    {
        // Preserve the camera exactly at the cut point, discard earlier
        // samples, and renumber all surviving .3ct frames so the old current
        // frame becomes the new visible frame 1.
        Pmvie()->TrimCameraTrackBefore(Pmvie()->Iscen(), _nfrmCur - _nfrmFirst + 1);
        _nfrmFirst = _nfrmCur;
        Pmvie()->SetDirty();
    }

    //
    // 3DMMv1.0: If _psseBkgd got chopped, get rid of it
    //
    if (_psseBkgd != pvNil && _nfrmSseBkgd < _nfrmFirst)
    {
        ReleasePpsse(&_psseBkgd);
        TrashVar(&_nfrmSseBkgd);
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * This routine chops off the rest of the scene, backwards,
 * and creates an undo object as well.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool SCEN::FChopBack()
{
    MVIE::MultiLog(Pmvie(),
        "scene_chop_before begin scene=%ld current=%ld first=%ld last=%ld camera_input=%d manual_frame=%d",
        (long)Pmvie()->Iscen(), (long)_nfrmCur, (long)_nfrmFirst, (long)_nfrmLast,
        (int)Pmvie()->FCameraInputActive(), (int)Pmvie()->FManualCameraFrameActive());

    if (_nfrmCur == _nfrmFirst)
    {
        MVIE::MultiLog(Pmvie(), "scene_chop_before noop already_first scene=%ld frame=%ld",
                       (long)Pmvie()->Iscen(), (long)_nfrmCur);
        return (fTrue);
    }

    if (!FAddSnapshotUndo(PszLit("Delete Everything Before")))
    {
        MVIE::MultiLog(Pmvie(), "scene_chop_before FAILED stage=undo scene=%ld frame=%ld",
                       (long)Pmvie()->Iscen(), (long)_nfrmCur);
        return (fFalse);
    }

    const int32_t nfrmCutVisible = _nfrmCur - _nfrmFirst + 1;
    if (!FChopBackCore())
    {
        Pmvie()->ClearUndo();
        MVIE::MultiLog(Pmvie(), "scene_chop_before FAILED stage=core scene=%ld cut_visible=%ld",
                       (long)Pmvie()->Iscen(), (long)nfrmCutVisible);
        return (fFalse);
    }

    Pmvie()->ApplyCameraTrack();
    Pmvie()->MarkViews();
    Pmvie()->Pmcc()->UpdateScrollbars();
    Pmvie()->InvalViewsAndScb();

    MVIE::MultiLog(Pmvie(),
        "scene_chop_before ok scene=%ld cut_visible=%ld new_current=%ld new_first=%ld new_last=%ld",
        (long)Pmvie()->Iscen(), (long)nfrmCutVisible, (long)_nfrmCur,
        (long)_nfrmFirst, (long)_nfrmLast);
    return (fTrue);
}

/****************************************************
 *
 * Add a complete current-scene undo snapshot, including the camera-track
 * sidecar state which SCEN::FWrite cannot serialize.
 *
 ****************************************************/
bool SCEN::FAddSnapshotUndo(const achar *pszUndoName)
{
    AssertThis(0);
    AssertSz(pszUndoName);

    PSUNC psunc = SUNC::PsuncNew();
    if (psunc == pvNil)
    {
        MVIE::MultiLog(Pmvie(), "scene_trim_snapshot FAILED stage=alloc scene=%ld frame=%ld",
                       (long)Pmvie()->Iscen(), (long)_nfrmCur);
        return fFalse;
    }

    psunc->SetUndoName(pszUndoName);
    if (!psunc->FSave(this))
    {
        MVIE::MultiLog(Pmvie(), "scene_trim_snapshot FAILED stage=save scene=%ld frame=%ld",
                       (long)Pmvie()->Iscen(), (long)_nfrmCur);
        ReleasePpo(&psunc);
        return fFalse;
    }

    if (!Pmvie()->FAddUndo(psunc))
    {
        MVIE::MultiLog(Pmvie(), "scene_trim_snapshot FAILED stage=add_undo scene=%ld frame=%ld",
                       (long)Pmvie()->Iscen(), (long)_nfrmCur);
        ReleasePpo(&psunc);
        return fFalse;
    }

    MVIE::MultiLog(Pmvie(), "scene_trim_snapshot ok scene=%ld frame=%ld",
                   (long)Pmvie()->Iscen(), (long)_nfrmCur);
    ReleasePpo(&psunc);
    return fTrue;
}

/***************************************************************************
    FAddNativeFrameUndo

    Add a lightweight undo record for the two original edge-extension paths.
    These operations do not edit actor routes, so a full serialized scene copy
    would add most of the cost we are specifically preserving the native path
    to avoid.
***************************************************************************/
bool SCEN::FAddNativeFrameUndo(bool fBefore, bool fBlank, int32_t cfrm)
{
    AssertThis(0);
    AssertIn(cfrm, 1, klwMax);

    PSUNF psunf = SUNF::PsunfNew();
    if (psunf == pvNil)
        return fFalse;

    bool fValid = psunf->FSave(this, fBefore, fBlank, cfrm);
    if (!fValid || !Pmvie()->FAddUndo(psunf))
    {
        ReleasePpo(&psunf);
        return fFalse;
    }

    ReleasePpo(&psunf);
    return fTrue;
}

/***************************************************************************
    FFrameBlank
***************************************************************************/
bool SCEN::FFrameBlank(int32_t nfrm) const
{
    SEV sev;
    for (int32_t isev = 0; isev < _pggsevFrm->IvMac(); isev++)
    {
        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.nfrm > nfrm)
            break;
        if (sev.nfrm == nfrm && sev.sevt == sevtBlankFrame)
            return fTrue;
    }
    return fFalse;
}

/***************************************************************************
    FNativeAppendFramesCore

    This is the original Ctrl+Next-at-end operation: FGotoFrm(current + 1).
    The optional blank flag only adds the scene-local blank marker before that
    same native seek; the nonblank path is otherwise unchanged.
***************************************************************************/
bool SCEN::FNativeAppendFramesCore(int32_t cfrm, bool fBlank)
{
    AssertThis(0);
    AssertIn(cfrm, 0, klwMax);

    if (cfrm <= 0)
        return fTrue;
    if (_nfrmCur != _nfrmLast || _nfrmCur > klwMax - cfrm)
        return fFalse;

    for (int32_t ifrm = 0; ifrm < cfrm; ifrm++)
    {
        int32_t nfrmNew = _nfrmCur + 1;
        if (fBlank)
        {
            SEV sev;
            sev.nfrm = nfrmNew;
            sev.sevt = sevtBlankFrame;
            int32_t isevIns = _pggsevFrm->IvMac();
            if (!_pggsevFrm->FInsert(isevIns, 0, pvNil, &sev))
                return fFalse;
        }

        // Exact original frame creation operation.
        if (!FGotoFrm(nfrmNew))
            return fFalse;
    }

    return fTrue;
}

/***************************************************************************
    FNativePrependBlankFramesCore

    This is the original Ctrl+Previous-at-first-frame operation repeated.
***************************************************************************/
bool SCEN::FNativePrependBlankFramesCore(int32_t cfrm)
{
    AssertThis(0);
    AssertIn(cfrm, 0, klwMax);

    if (cfrm <= 0)
        return fTrue;
    if (_nfrmCur != _nfrmFirst || _nfrmCur < klwMin + cfrm)
        return fFalse;

    for (int32_t ifrm = 0; ifrm < cfrm; ifrm++)
    {
        // Exact original blank-prepend operation.
        if (!FGotoFrm(_nfrmCur - 1))
            return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    FNativeRemoveEndFramesCore

    Undo only the empty timeline extension made by FNativeAppendFramesCore.
    Any later edit would be above this operation in the undo stack, so there
    are no actor-route edits to remove here.
***************************************************************************/
bool SCEN::FNativeRemoveEndFramesCore(int32_t cfrm)
{
    AssertThis(0);
    AssertIn(cfrm, 0, klwMax);

    if (cfrm <= 0)
        return fTrue;
    if (_nfrmLast - _nfrmFirst + 1 <= cfrm)
        return fFalse;

    int32_t nfrmLastNew = _nfrmLast - cfrm;
    if (_nfrmCur > nfrmLastNew && !FGotoFrm(nfrmLastNew))
        return fFalse;

    for (int32_t isev = _pggsevFrm->IvMac() - 1; isev >= 0; isev--)
    {
        SEV sev;
        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.nfrm <= nfrmLastNew)
            break;
        _pggsevFrm->Delete(isev);
    }

    _nfrmLast = nfrmLastNew;
    _MarkMovieDirty();
    return fTrue;
}

/***************************************************************************
    FNativeRemoveStartFramesCore

    Reverse the original blank-prepend operation.  Actors and text boxes were
    never moved by that operation; only the scene first bound and persistent
    first-frame camera event need to move forward again.
***************************************************************************/
bool SCEN::FNativeRemoveStartFramesCore(int32_t cfrm)
{
    AssertThis(0);
    AssertIn(cfrm, 0, klwMax);

    if (cfrm <= 0)
        return fTrue;
    if (_nfrmLast - _nfrmFirst + 1 <= cfrm)
        return fFalse;

    int32_t nfrmFirstOld = _nfrmFirst;
    int32_t nfrmFirstNew = _nfrmFirst + cfrm;
    if (_nfrmCur < nfrmFirstNew && !FGotoFrm(nfrmFirstNew))
        return fFalse;

    for (int32_t isev = _pggsevFrm->IvMac() - 1; isev >= 0; isev--)
    {
        SEV sev;
        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.nfrm < nfrmFirstOld)
        {
            _pggsevFrm->Delete(isev);
            continue;
        }
        if (sev.nfrm == nfrmFirstOld && sev.sevt == sevtChngCamera)
        {
            sev.nfrm = nfrmFirstNew;
            _pggsevFrm->PutFixed(isev, &sev);
        }
        else if (sev.nfrm < nfrmFirstNew)
        {
            _pggsevFrm->Delete(isev);
        }
    }

    _nfrmFirst = nfrmFirstNew;
    _isevFrmLim = 0;
    while (_isevFrmLim < _pggsevFrm->IvMac())
    {
        SEV sev;
        _pggsevFrm->GetFixed(_isevFrmLim, &sev);
        if (sev.nfrm > _nfrmCur)
            break;
        _isevFrmLim++;
    }
    _MarkMovieDirty();
    return fTrue;
}

/***************************************************************************
    FInsertFramesAtCurrent

    Insert held copies adjacent to the current frame.  After holds the current
    frame and inserts immediately following it.  Interior Before holds the
    preceding frame and inserts immediately before the selected frame, moving
    that selected old frame and every later frame forward by cfrm.
***************************************************************************/
bool SCEN::FInsertFramesAtCurrent(int32_t cfrm, bool fBefore, bool fBlank,
                                  bool fAddUndo)
{
    AssertThis(0);
    AssertIn(cfrm, 0, klwMax);

    if (cfrm <= 0)
        return fTrue;
    if (_pmvie == pvNil || _pmvie->FPlaying() || _nfrmCur > klwMax - cfrm)
        return fFalse;

    int32_t nfrmInsert = _nfrmCur;
    int32_t nfrmLastOld = _nfrmLast;
    int32_t nfrmTrack = nfrmInsert - _nfrmFirst + 1;

    // A true Before insertion is the gap immediately before the selected
    // frame.  For an interior frame, that is exactly the same timeline
    // boundary as an After insertion on the preceding frame: preserve the
    // preceding frame, insert held time after it, and move the selected old
    // frame (plus everything later) forward by cfrm.
    bool fInteriorBefore = fBefore && nfrmInsert > _nfrmFirst;
    int32_t nfrmHold = fInteriorBefore ? nfrmInsert - 1 : nfrmInsert;
    int32_t nfrmTrackHold = fInteriorBefore ? nfrmTrack - 1 : nfrmTrack;

    PACTR pactr;
    PTBOX ptbox;
    SEV sev;
    bool fHoldBlank = fFalse;
    for (int32_t isev = 0; isev < _pggsevFrm->IvMac(); isev++)
    {
        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.nfrm > nfrmHold)
            break;
        if (sev.nfrm == nfrmHold && sev.sevt == sevtBlankFrame)
        {
            fHoldBlank = fTrue;
            break;
        }
    }

    if (fAddUndo && !FAddSnapshotUndo(cfrm == 1 ? PszLit("Insert Frame")
                                                : PszLit("Insert Frames")))
        return fFalse;

    // Move later camera controls without manufacturing an exact manual-camera
    // sample on the inserted frame.  A tween crossing the gap gets a held
    // section so its old frames retain their old camera positions.
    bool fCameraShifted;
    if (fInteriorBefore)
        fCameraShifted = _pmvie->FInsertCameraTrackFramesAfter(_pmvie->Iscen(), nfrmTrackHold, cfrm);
    else if (fBefore)
        fCameraShifted = _pmvie->FInsertCameraTrackFramesBefore(_pmvie->Iscen(), nfrmTrack, cfrm);
    else
        fCameraShifted = _pmvie->FInsertCameraTrackFramesAfter(_pmvie->Iscen(), nfrmTrack, cfrm);
    if (!fCameraShifted)
    {
        // The snapshot at the top of the undo stack is also our atomic
        // rollback.  Never leave half-shifted camera or actor event streams.
        _pmvie->FUndo();
        _pmvie->ClearUndo();
        return fFalse;
    }

    for (int32_t iactr = 0; iactr < _pglpactr->IvMac(); iactr++)
    {
        _pglpactr->Get(iactr, &pactr);
        if (!pactr->FInsertHeldFramesAfter(nfrmHold, cfrm))
        {
            _pmvie->FUndo();
            _pmvie->ClearUndo();
            return fFalse;
        }
    }

    for (int32_t itbox = 0; itbox < _pglptbox->IvMac(); itbox++)
    {
        _pglptbox->Get(itbox, &ptbox);
        ptbox->InsertDuplicateFramesAfter(nfrmHold, cfrm);
    }

    // Shift ordinary frame events after the insertion point.  Events on the
    // copied frame remain on the first copy and their state persists through
    // the inserted hold frames.
    for (int32_t isev = 0; isev < _pggsevFrm->IvMac(); isev++)
    {
        _pggsevFrm->GetFixed(isev, &sev);
        if (sev.nfrm > nfrmHold)
        {
            sev.nfrm += cfrm;
            _pggsevFrm->PutFixed(isev, &sev);
        }
    }

    _nfrmLast = nfrmLastOld + cfrm;

    if (fBlank || fHoldBlank)
    {
        // A normal Shift insertion blanks only the newly inserted frames.  If
        // the held boundary frame was already blank, ordinary duplicate
        // insertion must preserve that visible state too.
        int32_t nfrmBlankFirst;
        // The inserted range always starts immediately after the held
        // boundary.  For interior Before this is the selected frame number;
        // for After it is selected+1.
        nfrmBlankFirst = nfrmHold + 1;
        for (int32_t ifrm = 0; ifrm < cfrm; ifrm++)
        {
            int32_t nfrmBlank = nfrmBlankFirst + ifrm;
            sev.nfrm = nfrmBlank;
            sev.sevt = sevtBlankFrame;
            int32_t isevIns = 0;
            for (; isevIns < _pggsevFrm->IvMac(); isevIns++)
            {
                SEV sevT;
                _pggsevFrm->GetFixed(isevIns, &sevT);
                if (sevT.nfrm > nfrmBlank)
                    break;
            }
            if (!_pggsevFrm->FInsert(isevIns, 0, pvNil, &sev))
            {
                _pmvie->FUndo();
                _pmvie->ClearUndo();
                return fFalse;
            }
        }
    }

    // Recompute the scene event cursor after shifting/inserting entries.
    _isevFrmLim = 0;
    while (_isevFrmLim < _pggsevFrm->IvMac())
    {
        _pggsevFrm->GetFixed(_isevFrmLim, &sev);
        if (sev.nfrm > _nfrmCur)
            break;
        _isevFrmLim++;
    }

    _MarkMovieDirty();
    // Before inserts duplicate time ahead of the old current frame, so follow
    // that original frame forward. After leaves the original current frame in
    // place and inserts the duplicate time following it.
    int32_t nfrmDest = fBefore ? nfrmInsert + cfrm : nfrmInsert;
    if (!FGotoFrm(nfrmDest))
    {
        // The snapshot at the top of the undo stack is also our atomic
        // rollback.  Never leave half-shifted camera or actor event streams.
        _pmvie->FUndo();
        _pmvie->ClearUndo();
        return fFalse;
    }

    _pmvie->ApplyCameraTrack();
    _pmvie->Pbwld()->Render();
    _pmvie->InvalViewsAndScb();
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine does any startup for playback
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   None.
 *
 ****************************************************/
bool SCEN::FStartPlaying()
{
    AssertThis(0);

    int32_t ipactr, iptbox;
    PACTR pactr;
    PTBOX ptbox;
    PTBXG ptbxg;
    int32_t nfrmFirst, nfrmLast;

    //
    // 3DMMv1.0: Make sure all actors have state variables updated
    // 3DMMv1.0: (they might not be in the theater right after
    // 3DMMv1.0: loading the movie)
    //
    for (ipactr = 0; ipactr < _pglpactr->IvMac(); ipactr++)
    {
        _pglpactr->Get(ipactr, &pactr);
        if (!pactr->FGetLifetime(&nfrmFirst, &nfrmLast))
            return fFalse;
    }

    for (iptbox = 0; iptbox < _pglptbox->IvMac(); iptbox++)
    {
        _pglptbox->Get(iptbox, &ptbox);
        if ((ptbox->FStory() && ptbox->FIsVisible()) ||
            (!ptbox->FStory() && ptbox->FGetLifetime(&nfrmFirst, &nfrmLast) && (nfrmFirst == _nfrmCur)))
        {
            ptbxg = (PTBXG)ptbox->PddgGet(0);
            ptbxg->Scroll(scaNil);
        }
    }

    //
    // 3DMMv1.0: Start prerendering at this frame, even if there's
    // 3DMMv1.0: no camera view change.
    //
    _DoPrerenderingWork(fTrue);
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * This routine cleans up after a playback has stopped.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   None.
 *
 ****************************************************/
void SCEN::StopPlaying()
{
    AssertThis(0);

    int32_t itbox;
    PTBOX ptbox;

    _EndPrerendering(); // 3DMMv1.0: Stop prerendering

    for (itbox = 0; itbox < _pglptbox->IvMac(); itbox++)
    {
        _pglptbox->Get(itbox, &ptbox);
        AssertPo(ptbox, 0);
        ptbox->CleanDdg();
    }
}

/** 3DMMv1.0: ****************************************************************************
    FTransOnFile
        For a given SCEN chunk on a given CRF, get the scene transition
        state for the scene.

    Arguments:
        PCRF pcrf     -- the chunky resource file the SCEN lives on
        CNO cno       -- the CNO of the SCEN chunk
        TRANS *ptrans -- pointer to memory to take the transition setting

    Returns: fTrue if it was able to set *ptrans, fFalse if something failed

************************************************************ PETED ***********/
bool SCEN::FTransOnFile(PCRF pcrf, CNO cno, TRANS *ptrans)
{
    BLCK blck;
    SCENH scenh;
    PCFL pcfl = pcrf->Pcfl();

    TrashVar(ptrans);

    if (!pcfl->FFind(kctgScen, cno, &blck))
        goto LFail;
    if (!blck.FUnpackData() || blck.Cb() != SIZEOF(SCENH))
        goto LFail;
    if (!blck.FReadRgb(&scenh, SIZEOF(SCENH), 0))
        goto LFail;

    *ptrans = scenh.trans;
    return fTrue;
LFail:
    return fFalse;
}

/** 3DMMv1.0: ****************************************************************************
    FSetTransOnFile
        For a given SCEN chunk on a given CRF, set the scene transition
        state for the scene.

    Arguments:
        PCRF pcrf   -- the chunky resource file the SCEN lives on
        CNO cno     -- the CNO of the SCEN chunk
        TRANS trans -- the transition state to use

    Returns: fTrue if the routine could guarantee that the scene has the
        given transition state.

************************************************************ PETED ***********/
bool SCEN::FSetTransOnFile(PCRF pcrf, CNO cno, TRANS trans)
{
    BLCK blck;
    SCENH scenh;
    PCFL pcfl = pcrf->Pcfl();

    if (!pcfl->FFind(kctgScen, cno, &blck))
        goto LFail;
    if (!blck.FUnpackData() || blck.Cb() != SIZEOF(SCENH))
        goto LFail;
    if (!blck.FReadRgb(&scenh, SIZEOF(SCENH), 0))
        goto LFail;
    if (scenh.trans != trans)
    {
        scenh.trans = trans;
        if (!pcfl->FPutPv(&scenh, SIZEOF(SCENH), kctgScen, cno))
            goto LFail;
    }

    return fTrue;
LFail:
    return fFalse;
}

//
//
//
// 3DMMv1.0: UNDO STUFF
//
//
//

/** 3DMMv1.0: **************************************************
 *
 * Public constructor for scene undo objects for name
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSUNT SUNT::PsuntNew()
{
    PSUNT psunt;
    psunt = NewObj SUNT();
    return (psunt);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for scene naming undo objects
 *
 ****************************************************/
SUNT::~SUNT(void)
{
    AssertBaseThis(0);
}

/** 3DMMv1.0: **************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
/***************************************************************************
    Plain-English history name.
***************************************************************************/
void SUNT::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    pstn->SetSz(PszLit("Rename Scene"));
}

bool SUNT::FDo(PDOCB pdocb)
{
    AssertThis(0);

    STN stn;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        _pmvie->Pmsq()->FlushMsq();
        _pmvie->ClearUndo();
        return (fFalse);
    }

    _pmvie->Pscen()->GetName(&stn);
    _pmvie->Pscen()->SetNameCore(&_stn);
    _stn = stn;

    _pmvie->Pmsq()->FlushMsq();

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SUNT::FUndo(PDOCB pdocb)
{
    AssertThis(0);

    return (FDo(pdocb));
}

#ifdef DEBUG
/** 3DMMv1.0: **************************************************
 * Mark memory used by the SUNT
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SUNT::MarkMem(void)
{
    AssertThis(0);
    SUNT_PAR::MarkMem();
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of the SUNT.
***************************************************************************/
void SUNT::AssertValid(uint32_t grf)
{
}
#endif

/** 3DMMv1.0: **************************************************
 *
 * Public constructor for scene undo objects for sound
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSUNS SUNS::PsunsNew()
{
    PSUNS psuns;
    psuns = NewObj SUNS();
    return (psuns);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for scene sound undo objects
 *
 ****************************************************/
SUNS::~SUNS(void)
{
    AssertBaseThis(0);
    ReleasePpsse(&_psse);
}

/** 3DMMv1.0: **************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
/***************************************************************************
    Plain-English history name.
***************************************************************************/
void SUNS::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    pstn->SetSz(PszLit("Edit Scene Sound"));
}

bool SUNS::FDo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);

    int32_t isevSnd = ivNil;
    bool fFound;
    PSSE psseOld = pvNil;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        _pmvie->ClearUndo();
        return (fFalse);
    }

    if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
    {
        _pmvie->ClearUndo();
        return (fFalse);
    }

    _pmvie->Pmsq()->FlushMsq();

    // 3DMMv1.0: swap the event in the event list (if any) with _psse (if any)

    if (!_pmvie->Pscen()->FGetSnd(_sty, &fFound, &psseOld))
    {
        _pmvie->ClearUndo();
        return (fFalse);
    }

    if (_psse != pvNil)
    {
        if (fFound)
        {
            _pmvie->Pscen()->RemSndCore(psseOld->sty);
            if (!_pmvie->Pscen()->FAddSndCoreTagc(_psse->fLoop, fFalse, _psse->vlm, _psse->sty, _psse->ctagc,
                                                  _psse->Ptagc(0)))
            {
                ReleasePpsse(&psseOld);
                _pmvie->ClearUndo();
                return (fFalse);
            }
            ReleasePpsse(&_psse);
            _psse = psseOld;
            _sty = psseOld->sty;
        }
        else // 3DMMv1.0: no sse to replace, just add this one
        {
            if (!_pmvie->Pscen()->FAddSndCoreTagc(_psse->fLoop, fFalse, _psse->vlm, _psse->sty, _psse->ctagc,
                                                  _psse->Ptagc(0)))
            {
                _pmvie->ClearUndo();
                return (fFalse);
            }
            ReleasePpsse(&_psse);
        }
    }
    else // 3DMMv1.0: _psse is pvNil...remember what's there then nuke the event
    {
        if (!fFound)
        {
            Bug("where's the sound event?");
            return (fFalse);
        }
        else
        {
            if (!FSetSnd(psseOld))
            {
                _pmvie->ClearUndo();
                return (fFalse);
            }
            ReleasePpsse(&psseOld);
            _pmvie->Pscen()->RemSndCore(_sty);
        }
    }

    _pmvie->Pmsq()->PlayMsq();

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SUNS::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);

    return FDo(pdocb);
}

#ifdef DEBUG
/** 3DMMv1.0: **************************************************
 * Mark memory used by the SUNS
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SUNS::MarkMem(void)
{
    AssertThis(0);
    SUNS_PAR::MarkMem();
    if (_psse != pvNil)
        MarkPv(_psse);
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of the SUNS.
***************************************************************************/
void SUNS::AssertValid(uint32_t grf)
{
    SUNS_PAR::AssertValid(grf);
    if (_psse != pvNil)
    {
        AssertPvCb(_psse, SIZEOF(SSE));
        AssertPvCb(_psse, _psse->Cb());
        Assert(_sty == _psse->sty, "sty's don't match");
    }
}
#endif

/** 3DMMv1.0: **************************************************
 *
 * Public constructor for scene undo objects for actor
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSUNA SUNA::PsunaNew()
{
    PSUNA psuna;
    psuna = NewObj SUNA();
    return (psuna);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for scene actor undo objects
 *
 ****************************************************/
SUNA::~SUNA(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pactr);
}

/** 3DMMv1.0: **************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
/***************************************************************************
    Plain-English history name for actor insertion/removal.
***************************************************************************/
void SUNA::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    switch (_ut)
    {
    case utAdd:
        pstn->SetSz(PszLit("Add Actor"));
        break;
    case utDel:
        pstn->SetSz(PszLit("Delete Actor"));
        break;
    case utRep:
        pstn->SetSz(PszLit("Replace Actor"));
        break;
    default:
        pstn->SetSz(PszLit("Edit Actor"));
        break;
    }
}

bool SUNA::FDo(PDOCB pdocb)
{
    AssertThis(0);

    PACTR pactr, pactrDup;
    MVIE::LightEditorLog(_pmvie, "actor_undo redo begin ut=%ld arid=%ld target_scene=%ld target_frame=%ld stored_ptr=%p",
                         (long)_ut, _pactr != pvNil ? (long)_pactr->Arid() : (long)aridNil,
                         (long)_iscen, (long)_nfrm, (void *)_pactr);

    if (!_pmvie->FSwitchScen(_iscen))
    {
        goto LFail;
    }

    if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
    {
        goto LFail;
    }

    if (_ut == utAdd)
    {

        if (!_pmvie->Pscen()->FAddActrCore(_pactr))
        {
            goto LFail;
        }

        if (_fHasLightLab)
        {
            LIGHTLAB light;
            ClearPb(&light, SIZEOF(light));
            light.iscen = _iscen;
            light.arid = _pactr->Arid();
            light.fEnabled = _fLightEnabled;
            light.fGenerateShadows = _fLightGenerateShadows;
            light.fAttachmentHideable = _fLightAttachmentHideable;
            light.intensity = _lightIntensity;
            light.edgeGradient = _lightEdgeGradient;
            light.diameter = _lightDiameter;
            light.range = _lightRange;
            CopyPb(_szLightShape, light.szShape, SIZEOF(light.szShape));
            if (!_pmvie->FRestoreLightLabConfigCore(&light))
                goto LFail;
        }

        if (_fHasObjectProperties)
        {
            OBJECTPROPERTIES prop;
            ClearPb(&prop, SIZEOF(prop));
            prop.iscen = _iscen;
            prop.arid = _pactr->Arid();
            prop.fFlushOverlap = _fObjectFlushOverlap;
            prop.fCastShadows = _fObjectCastShadows;
            if (!_pmvie->FSetObjectProperties(&prop, fFalse))
                goto LFail;
        }

        _pmvie->Pscen()->SelectActr(_pactr);

        if (!_pactr->FDup(&pactrDup, fTrue))
        {
            goto LFail;
        }

        pactrDup->SetArid(_pactr->Arid());
        ReleasePpo(&_pactr);
        SetActr(pactrDup);
    }
    else if (_ut == utDel)
    {

        _pmvie->Pscen()->RemActrCore(_pactr->Arid());
    }
    else
    {
        Assert(_ut == utRep, "Bad Grf");

        pactr = _pmvie->Pscen()->PactrFromArid(_pactr->Arid());
        if (pactr == pvNil)
        {
            MVIE::LightEditorLog(_pmvie, "actor_undo redo missing_live_actor arid=%ld", (long)_pactr->Arid());
            goto LFail;
        }
        if (!pactr->FDup(&pactrDup, fTrue))
        {
            goto LFail;
        }

        if (!_pmvie->Pscen()->FAddActrCore(_pactr))
        {
            goto LFail;
        }

        _pmvie->Pscen()->SelectActr(_pactr);

        pactrDup->SetArid(_pactr->Arid());
        ReleasePpo(&_pactr);
        SetActr(pactrDup);
    }

    _pmvie->Pmsq()->FlushMsq();
    _pmvie->Pmcc()->UpdateRollCall();
    _pmvie->UpdateTestLightAttachment();
    MVIE::LightEditorLog(_pmvie, "actor_undo redo success ut=%ld", (long)_ut);

    return (fTrue);

LFail:
    MVIE::LightEditorLog(_pmvie, "actor_undo redo FAILED ut=%ld", (long)_ut);
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SUNA::FUndo(PDOCB pdocb)
{
    AssertThis(0);

    PACTR pactr, pactrDup;
    MVIE::LightEditorLog(_pmvie, "actor_undo undo begin ut=%ld arid=%ld target_scene=%ld target_frame=%ld stored_ptr=%p",
                         (long)_ut, _pactr != pvNil ? (long)_pactr->Arid() : (long)aridNil,
                         (long)_iscen, (long)_nfrm, (void *)_pactr);

    if (!_pmvie->FSwitchScen(_iscen))
    {
        goto LFail;
    }

    if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
    {
        goto LFail;
    }

    if (_ut == utAdd)
    {
        if (_fHasLightLab)
            _pmvie->FRemoveLightLabConfigCore(_iscen, _pactr->Arid());
        if (_fHasObjectProperties)
        {
            OBJECTPROPERTIES prop;
            ClearPb(&prop, SIZEOF(prop));
            prop.iscen = _iscen;
            prop.arid = _pactr->Arid();
            prop.fFlushOverlap = fFalse;
            prop.fCastShadows = fTrue;
            if (!_pmvie->FSetObjectProperties(&prop, fFalse))
                goto LFail;
        }
        _pmvie->Pscen()->RemActrCore(_pactr->Arid());
    }
    else if (_ut == utDel)
    {

        if (!_pactr->FDup(&pactrDup, fTrue))
        {
            goto LFail;
        }

        pactrDup->SetArid(_pactr->Arid());

        if (!_pmvie->Pscen()->FAddActrCore(pactrDup))
        {
            ReleasePpo(&pactrDup);
            goto LFail;
        }

        _pmvie->Pscen()->SelectActr(pactrDup);

        ReleasePpo(&pactrDup);
    }
    else
    {
        Assert(_ut == utRep, "Bad Grf");

        pactr = _pmvie->Pscen()->PactrFromArid(_pactr->Arid());
        if (pactr == pvNil)
        {
            MVIE::LightEditorLog(_pmvie, "actor_undo undo missing_live_actor arid=%ld", (long)_pactr->Arid());
            goto LFail;
        }
        if (!pactr->FDup(&pactrDup, fTrue))
        {
            goto LFail;
        }

        if (!_pmvie->Pscen()->FAddActrCore(_pactr))
        {
            goto LFail;
        }

        _pmvie->Pscen()->SelectActr(_pactr);

        pactrDup->SetArid(_pactr->Arid());
        ReleasePpo(&_pactr);
        SetActr(pactrDup);
    }

    _pmvie->Pmsq()->FlushMsq();
    _pmvie->Pmcc()->UpdateRollCall();
    _pmvie->UpdateTestLightAttachment();
    MVIE::LightEditorLog(_pmvie, "actor_undo undo success ut=%ld", (long)_ut);

    return (fTrue);

LFail:
    MVIE::LightEditorLog(_pmvie, "actor_undo undo FAILED ut=%ld", (long)_ut);
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    return (fFalse);
}

#ifdef DEBUG
/** 3DMMv1.0: **************************************************
 * Mark memory used by the SUNA
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SUNA::MarkMem(void)
{
    AssertThis(0);
    SUNA_PAR::MarkMem();
    MarkMemObj(_pactr);
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of the SUNA.
***************************************************************************/
void SUNA::AssertValid(uint32_t grf)
{
    AssertPo(_pactr, 0);
}
#endif

/** 3DMMv1.0: **************************************************
 *
 * Public constructor for scene undo objects for text box
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSUNX SUNX::PsunxNew()
{
    PSUNX psunx;
    psunx = NewObj SUNX();
    return (psunx);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for scene text box undo objects
 *
 ****************************************************/
SUNX::~SUNX(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_ptbox);
}

/** 3DMMv1.0: **************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
/***************************************************************************
    Plain-English history name for text box insertion/removal.
***************************************************************************/
void SUNX::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    pstn->SetSz(_fAdd ? PszLit("Add Text Box") : PszLit("Delete Text Box"));
}

bool SUNX::FDo(PDOCB pdocb)
{
    AssertThis(0);

    PTBOX ptbox;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        goto LFail;
    }

    if (_fAdd)
    {

        if (!_pmvie->Pscen()->FGotoFrm(_nfrmFirst))
        {
            goto LFail;
        }

        if (!_pmvie->Pscen()->FAddTboxCore(_ptbox))
        {
            _pmvie->Pscen()->FGotoFrm(_nfrm);
            goto LFail;
        }

        if (_nfrmLast < _pmvie->Pscen()->NfrmLast())
        {
            AssertDo(_ptbox->FGotoFrame(_nfrmLast + 1), "Should never fail");
            _ptbox->HideCore();
        }

        _pmvie->Pscen()->SelectTbox(_ptbox);

        if (_pmvie->Pscen()->FGotoFrm(_nfrm))
        {

            //
            // 3DMMv1.0: Find the new itbox for this tbox.
            //
            for (_itbox = 0;; _itbox++)
            {

                ptbox = _pmvie->Pscen()->PtboxFromItbox(_itbox);
                AssertPo(ptbox, 0);

                if (ptbox == _ptbox)
                {
                    break;
                }
            }
        }
    }
    else
    {

        if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
        {
            goto LFail;
        }

        //
        // 3DMMv1.0: NOTE: ptbox may be different than _ptbox since
        // 3DMMv1.0: we may have switched away from the scene.
        //
        ptbox = _pmvie->Pscen()->PtboxFromItbox(_itbox);
        if (!_pmvie->Pscen()->FRemTboxCore(ptbox))
        {
            goto LFail;
        }
    }

    _pmvie->Pmsq()->FlushMsq();

    return (fTrue);

LFail:
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SUNX::FUndo(PDOCB pdocb)
{
    AssertThis(0);

    PTBOX ptbox;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        goto LFail;
    }

    if (_fAdd)
    {

        if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
        {
            goto LFail;
        }

        //
        // 3DMMv1.0: NOTE: ptbox may be different than _ptbox since
        // 3DMMv1.0: we may have switched away from the scene.
        //
        ptbox = _pmvie->Pscen()->PtboxFromItbox(_itbox);
        if (!_pmvie->Pscen()->FRemTboxCore(ptbox))
        {
            ReleasePpo(&ptbox);
            goto LFail;
        }
    }
    else
    {

        if (!_pmvie->Pscen()->FGotoFrm(_nfrmFirst))
        {
            goto LFail;
        }

        if (!_pmvie->Pscen()->FAddTboxCore(_ptbox))
        {
            goto LFail;
        }

        if (_nfrmLast < _pmvie->Pscen()->NfrmLast())
        {
            AssertDo(_ptbox->FGotoFrame(_nfrmLast + 1), "Should never fail");
            _ptbox->HideCore();
        }
        _pmvie->Pscen()->SelectTbox(_ptbox);

        if (_pmvie->Pscen()->FGotoFrm(_nfrm))
        {

            //
            // 3DMMv1.0: Find the new itbox for this tbox.
            //
            for (_itbox = 0;; _itbox++)
            {

                ptbox = _pmvie->Pscen()->PtboxFromItbox(_itbox);
                AssertPo(ptbox, 0);

                if (ptbox == _ptbox)
                {
                    break;
                }
            }
        }
    }

    _pmvie->Pmsq()->FlushMsq();

    return (fTrue);

LFail:
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    return (fFalse);
}
#ifdef DEBUG
/** 3DMMv1.0: **************************************************
 * Mark memory used by the SUNX
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SUNX::MarkMem(void)
{
    AssertThis(0);
    SUNX_PAR::MarkMem();
    MarkMemObj(_ptbox);
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of the SUNX.
***************************************************************************/
void SUNX::AssertValid(uint32_t grf)
{
    AssertPo(_ptbox, 0);
}
#endif

/** 3DMMv1.0: **************************************************
 *
 * Public constructor for scene undo objects for transition
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSUNR SUNR::PsunrNew()
{
    PSUNR psunr;
    psunr = NewObj SUNR();
    return (psunr);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for scene transition undo objects
 *
 ****************************************************/
SUNR::~SUNR(void)
{
    AssertBaseThis(0);
}

/** 3DMMv1.0: **************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
/***************************************************************************
    Plain-English history name.
***************************************************************************/
void SUNR::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    pstn->SetSz(PszLit("Change Scene Transition"));
}

bool SUNR::FDo(PDOCB pdocb)
{
    AssertThis(0);

    TRANS trans;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        goto LFail;
    }

    if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
    {
        goto LFail;
    }

    trans = _pmvie->Pscen()->Trans();
    _pmvie->Pscen()->SetTransitionCore(_trans);
    _trans = trans;

    _pmvie->Pmsq()->FlushMsq();

    return (fTrue);

LFail:
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SUNR::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    return (FDo(pdocb));
}

#ifdef DEBUG
/** 3DMMv1.0: **************************************************
 * Mark memory used by the SUNR
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SUNR::MarkMem(void)
{
    AssertThis(0);
    SUNR_PAR::MarkMem();
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of the SUNR.
***************************************************************************/
void SUNR::AssertValid(uint32_t grf)
{
}
#endif

/** 3DMMv1.0: **************************************************
 *
 * Public constructor for scene undo objects for pause
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSUNP SUNP::PsunpNew()
{
    PSUNP psunp;
    psunp = NewObj SUNP();
    return (psunp);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for scene pause undo objects
 *
 ****************************************************/
SUNP::~SUNP(void)
{
    AssertBaseThis(0);
}

/** 3DMMv1.0: **************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
/***************************************************************************
    Plain-English history name for pauses.
***************************************************************************/
void SUNP::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    pstn->SetSz(_fAdd ? PszLit("Add Pause") : PszLit("Edit Pause"));
}

bool SUNP::FDo(PDOCB pdocb)
{
    AssertThis(0);

    WIT wit = _wit;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        goto LFail;
    }

    if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
    {
        goto LFail;
    }

    if (!_pmvie->Pscen()->FPauseCore(&_wit, &_dts))
    {
        goto LFail;
    }

    _pmvie->Pmcc()->PauseType(wit);
    _pmvie->Pmsq()->FlushMsq();

    return (fTrue);

LFail:
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SUNP::FUndo(PDOCB pdocb)
{
    return (FDo(pdocb));
}

#ifdef DEBUG
/** 3DMMv1.0: **************************************************
 * Mark memory used by the SUNP
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SUNP::MarkMem(void)
{
    AssertThis(0);
    SUNP_PAR::MarkMem();
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of the SUNP.
***************************************************************************/
void SUNP::AssertValid(uint32_t grf)
{
}
#endif

/** 3DMMv1.0: **************************************************
 *
 * Public constructor for scene undo objects for background
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSUNK SUNK::PsunkNew()
{
    PSUNK psunk;
    psunk = NewObj SUNK();
    return (psunk);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for scene background undo objects
 *
 ****************************************************/
SUNK::~SUNK(void)
{
    AssertBaseThis(0);
}

/** 3DMMv1.0: **************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
/***************************************************************************
    Plain-English history name for background/camera edits.
***************************************************************************/
void SUNK::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    pstn->SetSz(_fSetBkgd ? PszLit("Change Background") : PszLit("Change Camera"));
}

bool SUNK::FDo(PDOCB pdocb)
{
    AssertThis(0);

    TAG tagOld;
    int32_t icamOld, icam;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        goto LFail;
    }

    if (_fSetBkgd)
    {

        if (_pmvie->Pscen()->Pbkgd() != pvNil)
        {
            icam = _pmvie->Pscen()->Pbkgd()->Icam();
        }

        if (!_pmvie->Pscen()->FSetBkgdCore(&_tag, &tagOld))
        {
            goto LFail;
        }

        if (!_pmvie->Pscen()->FChangeCamCore(_icam, &icamOld))
        {
            goto LFail;
        }

        _tag = tagOld;
        _icam = icam;
    }
    else
    {

        if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
        {
            goto LFail;
        }

        if (!_pmvie->Pscen()->FChangeCamCore(_icam, &icamOld))
        {
            goto LFail;
        }

        _icam = icamOld;
    }

    _pmvie->Pmsq()->FlushMsq();

    return (fTrue);

LFail:
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SUNK::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    return (FDo(pdocb));
}

#ifdef DEBUG
/** 3DMMv1.0: **************************************************
 * Mark memory used by the SUNK
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SUNK::MarkMem(void)
{
    AssertThis(0);
    SUNK_PAR::MarkMem();
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of the SUNK.
***************************************************************************/
void SUNK::AssertValid(uint32_t grf)
{
}
#endif

/****************************************************
 * Lightweight native-frame undo object.
 ****************************************************/
PSUNF SUNF::PsunfNew(void)
{
    return NewObj SUNF();
}

SUNF::~SUNF(void)
{
    AssertBaseThis(0);
    FreePpv((void **)&_pctstate);
}

bool SUNF::FSave(PSCEN pscen, bool fBefore, bool fBlank, int32_t cfrm)
{
    AssertPo(pscen, 0);
    AssertIn(cfrm, 1, klwMax);

    // Appending at the end never mutates .3ct data; camera state simply holds
    // forward.  Avoid the 140 KB camera snapshot on that original instant path.
    // Prepending does renumber camera controls and therefore needs the snapshot.
    if (fBefore)
    {
        if (!FAllocPv((void **)&_pctstate, SIZEOF(CTSTATE), fmemClear,
                      mprNormal))
        {
            return fFalse;
        }
        if (!pscen->Pmvie()->FGetCameraTrackState(_pctstate))
            return fFalse;
        _fHasCameraState = fTrue;
    }

    _fBefore = fBefore;
    _fBlank = fBlank;
    _cfrm = cfrm;
    _fInserted = fTrue;
    return fTrue;
}

void SUNF::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    if (_fBefore || _fBlank)
        pstn->SetSz(_cfrm == 1 ? PszLit("Insert Empty Frame")
                               : PszLit("Insert Empty Frames"));
    else
        pstn->SetSz(_cfrm == 1 ? PszLit("Insert Frame")
                               : PszLit("Insert Frames"));
}

bool SUNF::FDo(PDOCB pdocb)
{
    AssertThis(0);

    PSCEN pscen = pvNil;
    bool fRet = fFalse;
    if (!_pmvie->FSwitchScen(_iscen))
        goto LFail;

    pscen = _pmvie->Pscen();
    if (_fInserted)
    {
        fRet = _fBefore ? pscen->FNativeRemoveStartFramesCore(_cfrm)
                        : pscen->FNativeRemoveEndFramesCore(_cfrm);
    }
    else
    {
        fRet = _fBefore ? pscen->FNativePrependBlankFramesCore(_cfrm)
                        : pscen->FNativeAppendFramesCore(_cfrm, _fBlank);
    }
    if (!fRet)
        goto LFail;

    if (_fHasCameraState)
        _pmvie->SwapCameraTrackState(_pctstate);
    _fInserted = !_fInserted;
    _pmvie->SetDirty();
    _pmvie->ApplyCameraTrack();
    _pmvie->InvalViewsAndScb();
    _pmvie->Pmsq()->FlushMsq();
    return fTrue;

LFail:
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    return fFalse;
}

bool SUNF::FUndo(PDOCB pdocb)
{
    return FDo(pdocb);
}

#ifdef DEBUG
void SUNF::MarkMem(void)
{
    AssertThis(0);
    SUNF_PAR::MarkMem();
    if (_pctstate != pvNil)
        MarkPv(_pctstate);
}

void SUNF::AssertValid(uint32_t grf)
{
    SUNF_PAR::AssertValid(grf);
    AssertIn(_cfrm, 1, klwMax);
}
#endif // DEBUG

/** 3DMMv1.0: **************************************************
 *
 * Public constructor for scene undo objects for background
 * chop commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSUNC SUNC::PsuncNew()
{
    PSUNC psunc;
    psunc = NewObj SUNC();
    return (psunc);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for scene pause undo objects
 *
 ****************************************************/
SUNC::~SUNC(void)
{
    AssertBaseThis(0);
    // The undo snapshot is deliberately an orphan SCEN in the movie's normal
    // autosave CRF.  Remove it when the undo record leaves history so these
    // private snapshots do not accumulate for the rest of the editing session.
    if (_pcrf != pvNil && _cno != cnoNil && _pcrf->Pcfl() != pvNil &&
        _pcrf->Pcfl()->FFind(kctgScen, _cno))
    {
        _pcrf->Pcfl()->Delete(kctgScen, _cno);
    }
    _cno = cnoNil;
    ReleasePpo(&_pcrf);
    FreePpv((void **)&_pctstate);
}

/** 3DMMv1.0: **************************************************
 *
 * This function saves away a copy of the scene.
 *
 * Parameters:
 *  pscen - The scene to save away.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SUNC::FSave(PSCEN pscen)
{
    AssertPo(pscen, 0);
    PMVIE pmvie = pscen->Pmvie();
    PCRF pcrfAuto = pvNil;

    MVIE::MultiLog(pmvie, "scene_trim_snapshot save begin scene=%ld frame=%ld",
                   (long)pmvie->Iscen(), (long)pscen->Nfrm());

    if (!FAllocPv((void **)&_pctstate, SIZEOF(CTSTATE), fmemClear, mprNormal))
    {
        MVIE::MultiLog(pmvie, "scene_trim_snapshot save FAILED stage=camera_alloc");
        return fFalse;
    }
    if (!pmvie->FGetCameraTrackState(_pctstate))
    {
        MVIE::MultiLog(pmvie, "scene_trim_snapshot save FAILED stage=camera_state");
        return fFalse;
    }

    // The patch-248 runtime reaches this snapshot path but never reaches
    // FAddUndo.  Its one unusual boundary was writing SCEN::FWrite into a
    // brand-new empty CRF, while normal scene save/insert writes into the
    // movie's authoritative autosave CRF.  Keep the snapshot as a private
    // loner SCEN there so it uses the same proven resource/tag universe.
    if (!pmvie->FEnsureAutosave(&pcrfAuto) || pcrfAuto == pvNil)
    {
        MVIE::MultiLog(pmvie, "scene_trim_snapshot save FAILED stage=autosave");
        return fFalse;
    }
    pcrfAuto->AddRef();
    _pcrf = pcrfAuto;

    vpappb->BeginLongOp();
    _cno = cnoNil;
    const bool fRet = pscen->FWrite(_pcrf, &_cno);
    vpappb->EndLongOp();

    MVIE::MultiLog(pmvie, "scene_trim_snapshot save %s cno=%ld",
                   fRet ? "ok" : "FAILED", (long)_cno);
    return fRet;
}

/** 3DMMv1.0: **************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
/***************************************************************************
    Plain-English history name.
***************************************************************************/
void SUNC::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    if (_stnUndoName.Cch() > 0)
        *pstn = _stnUndoName;
    else
        pstn->SetSz(PszLit("Chop Scene"));
}

bool SUNC::FDo(PDOCB pdocb)
{
    AssertThis(0);

    PSCEN pscen;
    PSCEN pscenSave;

    MVIE::MultiLog(_pmvie, "scene_trim_undo_swap begin scene=%ld frame=%ld snapshot_cno=%ld",
                   (long)_iscen, (long)_nfrm, (long)_cno);
    vpappb->BeginLongOp();

    pscen = SCEN::PscenRead(_pmvie, _pcrf, _cno);

    if (pscen == pvNil)
    {
        goto LFail;
    }

    if (!pscen->FPlayStartEvents())
    {
        SCEN::Close(&pscen);
        goto LFail;
    }

    if (!_pmvie->FSwitchScen(_iscen))
    {
        SCEN::Close(&pscen);
        goto LFail;
    }

    pscenSave = _pmvie->Pscen();
    pscenSave->AddRef();

    if (!_pmvie->FInsScenCore(_iscen, pscen))
    {
        ReleasePpo(&pscenSave);
        SCEN::Close(&pscen);
        goto LFail;
    }

    SCEN::Close(&pscen);

    _pcrf->Pcfl()->Delete(kctgScen, _cno);
    _cno = cnoNil;
    pscenSave->SetNfrmCur(pscenSave->NfrmFirst() - 1);
    if (!pscenSave->FWrite(_pcrf, &_cno))
    {
        _pmvie->FRemScenCore(_iscen + 1);
        SCEN::Close(&pscenSave);
        goto LFail;
    }

    SCEN::Close(&pscenSave);

    if (!_pmvie->FRemScenCore(_iscen + 1))
    {
        goto LFail;
    }

    // The SCEN chunk does not contain .3ct data.  Swap the camera-track
    // snapshot alongside the scene so the same object supports both undo and
    // redo without a second special-case history system.
    _pmvie->SwapCameraTrackState(_pctstate);
    _pmvie->SetDirty();

    //
    // 3DMMv1.0: We don't care if we can't switch, everything was restored.
    //
    if (_pmvie->FSwitchScen(_iscen))
    {
        _pmvie->Pscen()->FGotoFrm(_nfrm);
        _pmvie->ApplyCameraTrack();
        _pmvie->MarkViews();
        _pmvie->Pmcc()->UpdateScrollbars();
        _pmvie->InvalViewsAndScb();
    }

    vpappb->EndLongOp();
    MVIE::MultiLog(_pmvie, "scene_trim_undo_swap ok scene=%ld frame=%ld snapshot_cno=%ld",
                   (long)_iscen, (long)_nfrm, (long)_cno);
    return (fTrue);

LFail:
    MVIE::MultiLog(_pmvie, "scene_trim_undo_swap FAILED scene=%ld frame=%ld snapshot_cno=%ld",
                   (long)_iscen, (long)_nfrm, (long)_cno);
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo();
    vpappb->EndLongOp();
    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SUNC::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    return (FDo(pdocb));
}

#ifdef DEBUG
/** 3DMMv1.0: **************************************************
 * Mark memory used by the SUNC
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SUNC::MarkMem(void)
{
    AssertThis(0);
    SUNC_PAR::MarkMem();
    MarkMemObj(_pcrf);
    MarkPv(_pctstate);
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of the SUNC.
***************************************************************************/
void SUNC::AssertValid(uint32_t grf)
{
    AssertNilOrPo(_pcrf, grf);
}
#endif

/** 3DMMv1.0: *************************************************************************
    Static function to allocate a SSE with room for ctag TAGs.
***************************************************************************/
PSSE SSE::PsseNew(int32_t ctag)
{
    Assert(ctag > 0, 0);

    PSSE psse;

    if (!FAllocPv((void **)&psse, _Cb(ctag), fmemNil, mprNormal))
        return pvNil;
    return psse;
}

/** 3DMMv1.0: *************************************************************************
    Creates a new SSE
***************************************************************************/
PSSE SSE::PsseNew(int32_t vlm, int32_t sty, bool fLoop, int32_t ctagc, TAGC *prgtagc)
{
    Assert(ctagc > 0, 0);
    AssertPvCb(prgtagc, LwMul(ctagc, SIZEOF(TAGC)));
    PSSE psse;
    int32_t itagc;

    psse = PsseNew(ctagc);
    if (pvNil == psse)
        return pvNil;
    psse->vlm = vlm;
    psse->sty = sty;
    psse->fLoop = fLoop;
    psse->ctagc = ctagc;
    for (itagc = 0; itagc < ctagc; itagc++)
    {
        *psse->Ptagc(itagc) = prgtagc[itagc];
        TAGM::DupTag(psse->Ptag(itagc));
    }
    return psse;
}

/** 3DMMv1.0: *************************************************************************
    Properly cleans up and frees a SSE
***************************************************************************/
void ReleasePpsse(PSSE *ppsse)
{
    AssertVarMem(ppsse);

    if (*ppsse == pvNil)
        return;

    PSSE psse = *ppsse;
    int32_t itagc;

    AssertIn(psse->ctagc, 0, 1000); // 3DMMv1.0: sanity check on ctagc
    AssertIn(psse->sty, styNil, styLim);

    for (itagc = 0; itagc < psse->ctagc; itagc++)
    {
        if (psse->Ptag(itagc)->pcrf != pvNil)
            TAGM::CloseTag(psse->Ptag(itagc));
    }

    FreePpv((void **)ppsse);
}

/** 3DMMv1.0: *************************************************************************
    Static function to allocate and read a SSE from a GG.  This is tricky
    because I can't do a pgg->Get() since the SSE is variable-sized, and
    I need to do QvGet twice since I'm allocating memory in this function.
***************************************************************************/
PSSE SSE::PsseDupFromGg(PGG pgg, int32_t iv, bool fDupTags)
{
    AssertPo(pgg, 0);
    AssertIn(iv, 0, pgg->IvMac());
    Assert(pgg->Cb(iv) >= SIZEOF(SSE), "variable part too small");

    int32_t ctagc;
    PSSE psse;
    int32_t itagc;

    ctagc = ((PSSE)pgg->QvGet(iv))->ctagc;

    psse = PsseNew(ctagc);
    CopyPb(pgg->QvGet(iv), psse, _Cb(ctagc));

    for (itagc = 0; itagc < psse->ctagc; itagc++)
    {
        if (fDupTags)
        {
            if (psse->Ptag(itagc)->sid == ksidUseCrf)
            {
                AssertPo(psse->Ptag(itagc)->pcrf, 0);
                TAGM::DupTag(psse->Ptag(itagc));
            }
        }
        else
        {
            // 3DMMv1.0: Clear the crf on read, since the caller isn't having us dupe the tag
            psse->Ptag(itagc)->pcrf = pvNil;
        }
    }

    return psse;
}

/** 3DMMv1.0: *************************************************************************
    Returns a PSSE just like this SSE except with ptag & chid added
***************************************************************************/
PSSE SSE::PsseAddTagChid(PTAG ptag, int32_t chid)
{
    AssertVarMem(ptag);

    PSSE psseNew;
    TAGC tagc;

    tagc.tag = *ptag;
    tagc.chid = chid;

    psseNew = SSE::PsseNew(ctagc + 1);
    if (pvNil == psseNew)
        return pvNil;

    CopyPb(this, psseNew, Cb());
    *psseNew->Ptagc(psseNew->ctagc) = tagc;
    psseNew->ctagc++;
    TAGM::DupTag(ptag);
    return psseNew;
}

/** 3DMMv1.0: *************************************************************************
    Return a duplicate of this SSE
***************************************************************************/
PSSE SSE::PsseDup(void)
{
    PSSE psse;
    int32_t itagc;

    if (!FAllocPv((void **)&psse, SIZEOF(SSE) + LwMul(ctagc, SIZEOF(TAGC)), fmemNil, mprNormal))
    {
        return pvNil;
    }
    CopyPb(this, psse, SIZEOF(SSE) + LwMul(ctagc, SIZEOF(TAGC)));
    for (itagc = 0; itagc < psse->ctagc; itagc++)
        TAGM::DupTag(psse->Ptag(itagc));
    return psse;
}

/** 3DMMv1.0: *************************************************************************
    Play all sounds in this SSE	-> Enqueue the sounds in the SSE
***************************************************************************/
void SSE::PlayAllSounds(PMVIE pmvie, uint32_t dtsStart)
{
    PMSND pmsnd;
    int32_t itag;
    int32_t tool = fLoop ? toolLooper : toolSounder;

    for (itag = 0; itag < ctagc; itag++)
    {
        if (Ptag(itag)->sid == ksidUseCrf)
        {
            if (!pmvie->FResolveSndTag(Ptag(itag), *Pchid(itag)))
                continue;
        }
        pmsnd = (PMSND)vptagm->PbacoFetch(Ptag(itag), MSND::FReadMsnd);
        if (pvNil == pmsnd)
            return;

        // 3DMMv1.0: Only queue if it's not the first sound; only start at dtsStart for first sound
        pmvie->Pmsq()->FEnqueue(pmsnd, 0, fLoop, (itag != 0), vlm, pmsnd->Spr(tool), fFalse,
                                (itag == 0 ? dtsStart : 0));

        ReleasePpo(&pmsnd);
    }
}
