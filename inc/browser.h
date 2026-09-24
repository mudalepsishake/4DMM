/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: ************************************************************

   Browser Class

    Author : *****
    Review Status: Reviewed

    Studio Independent Browsers:
    BASE --> CMH --> GOK	-->	BRWD  (Browser display class)
    BRWD --> BRWL  (Browser list class; chunky based)
    BRWD --> BRWT  (Browser text class)
    BRWD --> BRWL --> BRWN  (Browser named list class)

    Studio Dependent Browsers:
    BRWD --> BRWR  (Roll call class)
    BRWD --> BRWT --> BRWA  (Browser action class)
    BRWD --> BRWL --> BRWP	(Browser prop/actor class)
    BRWD --> BRWL --> BRWB	(Browser background class)
    BRWD --> BRWL --> BRWC	(Browser camera class)
    BRWD --> BRWL --> BRWN --> BRWM (Browser music class)
    BRWD --> BRWL --> BRWN --> BRWM --> BRWI (Browser import sound class)

    Note: An "frm" refers to the displayed frames on any page.
    A "thum" is a generic Browser Thumbnail, which may be a
    chid, cno, cnoPar, gob, stn, etc.	A browser will display,
    over multiple pages, as many browser entities as there are
    thum's.

***************************************************************/

#ifndef BRWD_H
#define BRWD_H

const int32_t kcmhlBrowser = 0x11000; // 3DMMv1.0: nice medium level for the Browser
const int32_t kcbMaxCrm = 300000;
const int32_t kdwTotalPhysLim = 10240000; // 3DMMv1.0: 10MB	heuristic
const int32_t kdwAvailPhysLim = 1024000;  // 3DMMv1.0: 1MB heuristic
const auto kBrwsScript = (kstDefault << 16) | kchidBrowserDismiss;

/** 3DMMv1.0: **********************************

    Browser Context	CLass
    Optional context to carry over
    between successive instantiations
    of the same browser

*************************************/
#define BRCN_PAR BASE
#define kclsBRCN KLCONST4('B', 'R', 'C', 'N')
typedef class BRCN *PBRCN;
class BRCN : public BRCN_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    BRCN(void){};

  public:
    int32_t brwdid;
    int32_t ithumPageFirst;
};

/** 3DMMv1.0: **********************************

   Browser ThumbFile Cki Struct

*************************************/
struct TFC
{
    int16_t bo;
    int16_t osk;
    union {
        struct
        {
            CTG ctg;
            CNO cno;
        };
        struct
        {
            uint32_t grfontMask;
            uint32_t grfont;
        };
        struct
        {
            CTG _ctg;
            CHID chid;
        };
    };
};
VERIFY_STRUCT_SIZE(TFC, 12);
const BOM kbomTfc = 0x5f000000;

/** 3DMMv1.0: **********************************

   Browser Display Class

*************************************/
#define BRWD_PAR GOK
#define kclsBRWD KLCONST4('B', 'R', 'W', 'D')
#define brwdidNil ivNil
typedef class BRWD *PBRWD;
class BRWD : public BRWD_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(BRWD)

  protected:
    int32_t _kidFrmFirst;     // 3DMMv1.0: kid of first frame
    int32_t _kidControlFirst; // 3DMMv1.0: kid of first control button
    int32_t _dxpFrmOffset;    // 3DMMv1.0: x inset of thumb in frame
    int32_t _dypFrmOffset;    // 3DMMv1.0: y inset of thumb in frame
    int32_t _sidDefault;      // 3DMMv1.0: default sid
    int32_t _thumDefault;     // 3DMMv1.0: default thum
    PBRCN _pbrcn;             // 3DMMv1.0: context carryover
    int32_t _idsFont;         // 3DMMv1.0: string id of Font
    int32_t _kidThumOverride; // 3DMMv1.0: projects may override one thum gobid
    int32_t _ithumOverride;   // 3DMMv1.0: projects may override one thum gobid
    PTGOB _ptgobPage;         // 3DMMv1.0: for page numbers
    PSTDIO _pstdio;

    // 3DMMv1.0: Display State variables
    int32_t _cthumCD;        // 3DMMv1.0: Non-user content
    int32_t _ithumSelect;    // 3DMMv1.0: Hilited frame
    int32_t _ithumPageFirst; // 3DMMv1.0: Index to thd of first frame on current page
    int32_t _cfrmPageCur;    // 3DMMv1.0: Number of visible thumbnails per current page
    int32_t _cfrm;           // 3DMMv1.0: Total frames possible per page
    int32_t _cthumScroll;    // 3DMMv1.0: #items to scroll on fwd/back.  default ivNil -> page scrolling
    bool _fWrapScroll;       // 3DMMv1.0: Wrap around.  Default = fTrue;
    bool _fNoRepositionSel;  // 3DMMv1.0: Don't reposition selection : default = fFalse;

  protected:
    void _SetScrollState(void);
    int32_t _CfrmCalc(void);
    static bool _FBuildGcb(GCB *pgcb, int32_t kidPar, int32_t kidBrws);
    bool _FInitGok(PRCA prca, int32_t kidGlass);
    void _SetVarForOverride(void);

    virtual int32_t _Cthum(void)
    {
        AssertThis(0);
        return 0;
    }
    virtual bool _FSetThumFrame(int32_t ithum, PGOB pgobPar)
    {
        AssertThis(0);
        return fFalse;
    }
    virtual bool _FClearHelp(int32_t ifrm)
    {
        return fTrue;
    }
    virtual void _ReleaseThumFrame(int32_t ifrm)
    {
    }
    virtual int32_t _IthumFromThum(int32_t thum, int32_t sid)
    {
        return thum;
    }
    virtual void _GetThumFromIthum(int32_t ithum, void *pThumSelect, int32_t *psid);
    virtual void _ApplySelection(int32_t thumSelect, int32_t sid)
    {
    }
    virtual void _ProcessSelection(void)
    {
    }
    virtual bool _FUpdateLists()
    {
        return fTrue;
    }
    virtual void _SetCbPcrmMin(void)
    {
    }
    void _CalcIthumPageFirst(void);
    bool _FIsIthumOverride(int32_t ithum)
    {
        return FPure(ithum == _ithumOverride);
    }
    PGOB _PgobFromIfrm(int32_t ifrm);
    int32_t _KidThumFromIfrm(int32_t ifrm);
    void _UnhiliteCurFrm(void);
    bool _FHiliteFrm(int32_t ifrmSelect);
    void _InitStateVars(PCMD pcmd, PSTDIO pstdio, bool fWrapScroll, int32_t cthumScroll);
    void _InitFromData(PCMD pcmd, int32_t ithumSelect, int32_t ithumDisplay);
    virtual void _CacheContext(void);

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWD(PGCB pgcb) : BRWD_PAR(pgcb)
    {
        _ithumOverride = -1;
        _kidThumOverride = -1;
    }
    ~BRWD(void);

    static PBRWD PbrwdNew(PRCA prca, int32_t kidPar, int32_t kidBrwd);
    void Init(PCMD pcmd, int32_t ithumSelect, int32_t ithumDisplay, PSTDIO pstdio, bool fWrapScroll = fTrue,
              int32_t cthumScroll = ivNil);
    bool FDraw(void);
    bool FCreateAllTgob(void); // 3DMMv1.0: For any text based browsers

    //
    // 3DMMv1.0: Command Handlers
    // 3DMMv1.0: Selection does not exit the browser
    //
    bool FCmdFwd(PCMD pcmd);  // 3DMMv1.0: Page fwd
    bool FCmdBack(PCMD pcmd); // 3DMMv1.0: Page back
    bool FCmdSelect(PCMD pcmd);
    bool FCmdSelectThum(PCMD pcmd); // 3DMMv1.0: Set viewing page
    virtual void Release(void) override;
    virtual bool FCmdCancel(PCMD pcmd); // 3DMMv1.0: See brwb
    virtual bool FCmdDel(PCMD pcmd)
    {
        return fTrue;
    } // 3DMMv1.0: See brwm
    virtual bool FCmdOk(PCMD pcmd);
    virtual bool FCmdFile(PCMD pcmd)
    {
        return fTrue;
    } // 3DMMv1.0: See brwm
    virtual bool FCmdChangeCel(PCMD pcmd)
    {
        return fTrue;
    } // 3DMMv1.0: See brwa
};

/** 3DMMv1.0: **********************************

    Browser List Context CLass
    Optional context to carry over
    between successive instantiations
    of the same browser

*************************************/
#define BRCNL_PAR BRCN
#define kclsBRCNL KLCONST4('b', 'r', 'c', 'l')
typedef class BRCNL *PBRCNL;
class BRCNL : public BRCNL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    ~BRCNL(void);

  public:
    int32_t cthumCD;
    CKI ckiRoot;
    PGL pglthd;
    PGST pgst;
    PCRM pcrm;
};

//
// 3DMMv1.0:	Thumbnail descriptors : one per thumbnail
//
const int32_t kglstnGrow = 5;
const int32_t kglthdGrow = 10;
struct THD
{
    union {
        TAG tag; // 3DMMv1.0: TAG pointing to content
        struct
        {
            int32_t lwFill1; // 3DMMv1.0: sid
            int32_t lwFill2; // 3DMMv1.0: pcrf
            uint32_t grfontMask;
            uint32_t grfont;
        };
        struct
        {
            int32_t _lwFill1;
            int32_t _lwFill2;
            CTG ctg;
            CHID chid; // 3DMMv1.0: CHID of CD content
        };
    };

    CNO cno;       // 3DMMv1.0: GOKD cno
    CHID chidThum; // 3DMMv1.0: GOKD's parent's CHID (relative to GOKD parent's parent)
    int32_t ithd;  // 3DMMv1.0: Original index for this THD, before sorting (used to
                   // 3DMMv1.0: retrieve proper STN for the BRWN-derived browsers)
};

/* 3DMMv1.0: Browser Content List Base --  create one of these when you want a list of a
    specific kind of content and you don't care about the names. */
#define BCL_PAR BASE
typedef class BCL *PBCL;
#define kclsBCL KLCONST3('B', 'C', 'L')
class BCL : public BCL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    CTG _ctgRoot;
    CNO _cnoRoot;
    CTG _ctgContent;
    bool _fDescend;
    PGL _pglthd;

  protected:
    BCL(void)
    {
        _pglthd = pvNil;
    }
    ~BCL(void)
    {
        ReleasePpo(&_pglthd);
    }

    bool _FInit(PCRM pcrm, CKI *pckiRoot, CTG ctgContent, PGL pglthd);
    bool _FAddGokdToThd(PCFL pcfl, int32_t sid, CKI *pcki);
    bool _FAddFileToThd(PCFL pcfl, int32_t sid);
    bool _FBuildThd(PCRM pcrm);

    virtual bool _FAddGokdToThd(PCFL pcfl, int32_t sid, KID *pkid);

  public:
    static PBCL PbclNew(PCRM pcrm, CKI *pckiRoot, CTG ctgContent, PGL pglthd = pvNil, bool fOnlineOnly = fFalse);

    PGL Pglthd(void)
    {
        return _pglthd;
    }
    void GetThd(int32_t ithd, THD *pthd)
    {
        _pglthd->Get(ithd, pthd);
    }
    int32_t IthdMac(void)
    {
        return _pglthd->IvMac();
    }
};

/* 3DMMv1.0: Browser Content List with Strings -- create one of these when you need to
    browse content by name */
#define BCLS_PAR BCL
typedef class BCLS *PBCLS;
#define kclsBCLS KLCONST4('B', 'C', 'L', 'S')
class BCLS : public BCLS_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PGST _pgst;

  protected:
    BCLS(void)
    {
        _pgst = pvNil;
    }
    ~BCLS(void)
    {
        ReleasePpo(&_pgst);
    }

    bool _FInit(PCRM pcrm, CKI *pckiRoot, CTG ctgContent, PGST pgst, PGL pglthd);
    bool _FSetNameGst(PCFL pcfl, CTG ctg, CNO cno);

    virtual bool _FAddGokdToThd(PCFL pcfl, int32_t sid, KID *pkid) override;

  public:
    static PBCLS PbclsNew(PCRM pcrm, CKI *pckiRoot, CTG ctgContent, PGL pglthd = pvNil, PGST pgst = pvNil,
                          bool fOnlineOnly = fFalse);

    PGST Pgst(void)
    {
        return _pgst;
    }
};

/** 3DMMv1.0: **********************************

   Browser List Class
   Derived from the Display Class

*************************************/
#define BRWL_PAR BRWD
#define kclsBRWL KLCONST4('B', 'R', 'W', 'L')
typedef class BRWL *PBRWL;

// 3DMMv1.0: Browser Selection Flags
// 3DMMv1.0: This specifies what the sorting is based on
enum BWS
{
    kbwsIndex = 1,
    kbwsChid = 2,
    kbwsCnoRoot = 3, // 3DMMv1.0: defaults to CnoRoot if ctg of Par is ctgNil
    kbwsLim
};

class BRWL : public BRWL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    bool _fEnableAccel;

    // 3DMMv1.0: Thumnail descriptor lists
    PCRM _pcrm;  // 3DMMv1.0: Chunky resource manager
    PGL _pglthd; // 3DMMv1.0: Thumbnail descriptor	gl
    PGST _pgst;  // 3DMMv1.0: Chunk name

    // 3DMMv1.0: Browser Search (List) parameters
    BWS _bws;         // 3DMMv1.0: Selection type flag
    bool _fSinglePar; // 3DMMv1.0: Single parent search
    CKI _ckiRoot;     // 3DMMv1.0: Grandparent cno=cnoNil => global search
    CTG _ctgContent;  // 3DMMv1.0: Parent

  protected:
    // 3DMMv1.0: BRWL List
    bool _FInitNew(PCMD pcmd, BWS bws, int32_t ThumSelect, CKI ckiRoot, CTG ctgContent);
    bool _FCreateBuildThd(CKI ckiRoot, CTG ctgContent, bool fBuildGl = fTrue);
    virtual bool _FGetContent(PCRM pcrm, CKI *pcki, CTG ctg, bool fBuildGl);
    virtual int32_t _Cthum(void) override
    {
        AssertThis(0);
        return _pglthd->IvMac();
    }
    virtual bool _FSetThumFrame(int32_t ithd, PGOB pgobPar) override;
    virtual bool _FUpdateLists() override
    {
        return fTrue;
    } // 3DMMv1.0: Eg, to include user sounds

    // 3DMMv1.0: BRWL util
    void _SortThd(void);
    virtual void _GetThumFromIthum(int32_t ithum, void *pThumSelect, int32_t *psid) override;
    virtual void _ReleaseThumFrame(int32_t ifrm) override;
    virtual int32_t _IthumFromThum(int32_t thum, int32_t sid) override;
    virtual void _CacheContext(void) override;
    virtual void _SetCbPcrmMin(void) override;

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWL(PGCB pgcb) : BRWL_PAR(pgcb)
    {
    }
    ~BRWL(void);

    static PBRWL PbrwlNew(PRCA prca, int32_t kidPar, int32_t kidBrwl);
    virtual bool FInit(PCMD pcmd, BWS bws, int32_t ThumSelect, int32_t sidSelect, CKI ckiRoot, CTG ctgContent,
                       PSTDIO pstdio, PBRCNL pbrcnl = pvNil, bool fWrapScroll = fTrue, int32_t cthumScroll = ivNil);
};

/** 3DMMv1.0: **********************************

   Browser Text Class
   Derived from the Display Class

*************************************/
#define BRWT_PAR BRWD
#define kclsBRWT KLCONST4('B', 'R', 'W', 'T')
typedef class BRWT *PBRWT;
class BRWT : public BRWT_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PGST _pgst;
    bool _fEnableAccel;

    virtual int32_t _Cthum(void) override
    {
        AssertThis(0);
        return _pgst->IvMac();
    }
    virtual bool _FSetThumFrame(int32_t istn, PGOB pgobPar) override;
    virtual void _ReleaseThumFrame(int32_t ifrm) override
    {
    } // 3DMMv1.0: No gob to release

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWT(PGCB pgcb) : BRWT_PAR(pgcb)
    {
        _idsFont = idsNil;
    }
    ~BRWT(void);

    static PBRWT PbrwtNew(PRCA prca, int32_t kidPar, int32_t kidBrwt);
    void SetGst(PGST pgst);
    bool FInit(PCMD pcmd, int32_t thumSelect, int32_t thumDisplay, PSTDIO pstdio, bool fWrapScroll = fTrue,
               int32_t cthumScroll = ivNil);
};

/** 3DMMv1.0: **********************************

   Browser Named List Class
   Derived from the Browser List Class

*************************************/
#define BRWN_PAR BRWL
#define kclsBRWN KLCONST4('B', 'R', 'W', 'N')
typedef class BRWN *PBRWN;
class BRWN : public BRWN_PAR
{
    RTCLASS_DEC

  protected:
    virtual bool _FGetContent(PCRM pcrm, CKI *pcki, CTG ctg, bool fBuildGl) override;
    virtual int32_t _Cthum(void) override
    {
        return _pglthd->IvMac();
    }
    virtual bool _FSetThumFrame(int32_t ithd, PGOB pgobPar) override;
    virtual void _ReleaseThumFrame(int32_t ifrm) override;

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWN(PGCB pgcb) : BRWN_PAR(pgcb)
    {
    }
    ~BRWN(void){};
    virtual bool FInit(PCMD pcmd, BWS bws, int32_t ThumSelect, int32_t sidSelect, CKI ckiRoot, CTG ctgContent,
                       PSTDIO pstdio, PBRCNL pbrcnl = pvNil, bool fWrapScroll = fTrue,
                       int32_t cthumScroll = ivNil) override;

    virtual bool FCmdOk(PCMD pcmd) override;
};

/** 3DMMv1.0: **********************************

   Studio Specific Browser Classes

*************************************/
/** 3DMMv1.0: **********************************

   Browser Action Class
   Derived from the Browser Text Class
   Actions are separately classed for
   previews

*************************************/
#define BRWA_PAR BRWT
#define kclsBRWA KLCONST4('B', 'R', 'W', 'A')
typedef class BRWA *PBRWA;
class BRWA : public BRWA_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    int32_t _celnStart;                    // 3DMMv1.0: Starting cel number
    PAPE _pape;                            // 3DMMv1.0: Actor Preview Entity
    void _ProcessSelection(void) override; // 3DMMv1.0: Action Preview
    virtual void _ApplySelection(int32_t thumSelect, int32_t sid) override;

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWA(PGCB pgcb) : BRWA_PAR(pgcb)
    {
        _idsFont = idsActionFont;
        _celnStart = 0;
    }
    ~BRWA(void)
    {
    }

    static PBRWA PbrwaNew(PRCA prca);
    bool FBuildApe(PACTR pactr);
    bool FBuildGst(PSCEN pscen);
    virtual bool FCmdChangeCel(PCMD pcmd) override;
};

/** 3DMMv1.0: **********************************

   Browser Prop & Actor Class
   Derived from the Browser List Class

*************************************/
#define BRWP_PAR BRWL
#define kclsBRWP KLCONST4('B', 'R', 'W', 'P')
typedef class BRWP *PBRWP;
class BRWP : public BRWP_PAR
{
    RTCLASS_DEC

  protected:
    virtual void _ApplySelection(int32_t thumSelect, int32_t sid) override;
    virtual bool _FUpdateLists() override;
    virtual bool _FSetThumFrame(int32_t ithd, PGOB pgobPar) override;

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWP(PGCB pgcb) : BRWP_PAR(pgcb)
    {
    }
    ~BRWP(void){};

    static PBRWP PbrwpNew(PRCA prca, int32_t kidGlass);
};

/** 3DMMv1.0: **********************************

   Browser Background Class
   Derived from the Browser List Class

*************************************/
#define BRWB_PAR BRWL
#define kclsBRWB KLCONST4('B', 'R', 'W', 'B')
typedef class BRWB *PBRWB;
class BRWB : public BRWB_PAR
{
    RTCLASS_DEC

  protected:
    virtual void _ApplySelection(int32_t thumSelect, int32_t sid) override;

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWB(PGCB pgcb) : BRWB_PAR(pgcb)
    {
    }
    ~BRWB(void){};

    static PBRWB PbrwbNew(PRCA prca);
    virtual bool FCmdCancel(PCMD pcmd) override;
};

/** 3DMMv1.0: **********************************

   Browser Camera Class
   Derived from the Browser List Class

*************************************/
#define BRWC_PAR BRWL
#define kclsBRWC KLCONST4('B', 'R', 'W', 'C')
typedef class BRWC *PBRWC;
class BRWC : public BRWC_PAR
{
    RTCLASS_DEC

  protected:
    virtual void _ApplySelection(int32_t thumSelect, int32_t sid) override;
    virtual void _SetCbPcrmMin(void) override
    {
    }

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWC(PGCB pgcb) : BRWC_PAR(pgcb)
    {
    }
    ~BRWC(void){};

    static PBRWC PbrwcNew(PRCA prca);

    virtual bool FCmdCancel(PCMD pcmd) override;
};

/** 3DMMv1.0: **********************************

   Browser Music Class (midi, speech & fx)
   Derived from the Browser Named List Class

*************************************/
#define BRWM_PAR BRWN
#define kclsBRWM KLCONST4('b', 'r', 'w', 'm')
typedef class BRWM *PBRWM;
class BRWM : public BRWM_PAR
{
    RTCLASS_DEC

  protected:
    int32_t _sty; // 3DMMv1.0: Identifies type of sound
    PCRF _pcrf;   // 3DMMv1.0: NOT created here (autosave or BRWI file)

    virtual void _ApplySelection(int32_t thumSelect, int32_t sid) override;
    virtual bool _FUpdateLists() override; // 3DMMv1.0: By all entries in pcrf of correct type
    void _ProcessSelection(void) override; // 3DMMv1.0: Sound Preview
    bool _FAddThd(STN *pstn, CKI *pcki);
    bool _FSndListed(CNO cno, int32_t *pithd = pvNil);

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWM(PGCB pgcb) : BRWM_PAR(pgcb)
    {
        _idsFont = idsSoundFont;
    }
    ~BRWM(void){};

    static PBRWM PbrwmNew(PRCA prca, int32_t kidGlass, int32_t sty, PSTDIO pstdio);
    virtual bool FCmdFile(PCMD pcmd) override; // 3DMMv1.0: Upon portfolio completion
    virtual bool FCmdDel(PCMD pcmd) override;  // 3DMMv1.0: Delete user sound
};

/** 3DMMv1.0: **********************************

   Browser Import Sound Class
   Derived from the Browser List Class
   Note: Inherits pgst from the list class

*************************************/
#define BRWI_PAR BRWM
#define kclsBRWI KLCONST4('B', 'R', 'W', 'I')
typedef class BRWI *PBRWI;
class BRWI : public BRWI_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // 3DMMv1.0: The following are already handled by BRWM
    // 3DMMEx: virtual void _ProcessSelection(void) override;
    virtual void _ApplySelection(int32_t thumSelect, int32_t sid) override;

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWI(PGCB pgcb) : BRWI_PAR(pgcb)
    {
        _idsFont = idsSoundFont;
    }
    ~BRWI(void);

    static PBRWI PbrwiNew(PRCA prca, int32_t kidGlass, int32_t sty);
    bool FInit(PCMD pcmd, CKI cki, PSTDIO pstdio);
};

/** 3DMMv1.0: **********************************

   Browser Roll Call Class
   Derived from the Display Class

*************************************/
#define BRWR_PAR BRWD
#define kclsBRWR KLCONST4('B', 'R', 'W', 'R')
typedef class BRWR *PBRWR;
class BRWR : public BRWR_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    CTG _ctg;
    PCRM _pcrm; // 3DMMv1.0: Chunky resource manager
    bool _fApplyingSel;

  protected:
    virtual int32_t _Cthum(void) override;
    virtual bool _FSetThumFrame(int32_t istn, PGOB pgobPar) override;
    virtual void _ReleaseThumFrame(int32_t ifrm) override;
    virtual void _ApplySelection(int32_t thumSelect, int32_t sid) override;
    virtual void _ProcessSelection(void) override;
    virtual bool _FClearHelp(int32_t ifrm) override;
    int32_t _IaridFromIthum(int32_t ithum, int32_t iaridFirst = 0);
    int32_t _IthumFromArid(int32_t arid);

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    BRWR(PGCB pgcb) : BRWR_PAR(pgcb)
    {
        _fApplyingSel = fFalse;
        _idsFont = idsRollCallFont;
    }
    ~BRWR(void);

    static PBRWR PbrwrNew(PRCA prca, int32_t kid);
    void Init(PCMD pcmd, int32_t thumSelect, int32_t thumDisplay, PSTDIO pstdio, bool fWrapScroll = fTrue,
              int32_t cthumScroll = ivNil);
    bool FInit(PCMD pcmd, CTG ctg, int32_t ithumDisplay, PSTDIO pstdio);
    bool FUpdate(int32_t arid, PSTDIO pstdio);
    bool FApplyingSel(void)
    {
        AssertBaseThis(0);
        return _fApplyingSel;
    }
};

const int32_t kglcmgGrow = 8;
struct CMG // 3DMMv1.0: Gokd Cno Map
{
    CNO cnoTmpl; // 3DMMv1.0: Content cno
    CNO cnoGokd; // 3DMMv1.0: Thumbnail gokd cno
};

/** 3DMMv1.0: **********************************

   Fne for  Thumbnails
   Enumerates current product first

*************************************/
#define FNET_PAR BASE
#define kclsFNET KLCONST4('F', 'N', 'E', 'T')
typedef class FNET *PFNET;
class FNET : public FNET_PAR
{
    RTCLASS_DEC

  protected:
    bool _fInitMSKDir;
    FNE _fne;
    FNE _fneDir;
    FNI _fniDirMSK;
    FNI _fniDir;
    FNI _fniDirProduct;
    bool _fInited;

  protected:
    bool _FNextFni(FNI *pfni, int32_t *psid);

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    FNET(void) : FNET_PAR()
    {
        _fInited = fFalse;
    }
    ~FNET(void){};

    bool FInit(void);
    bool FNext(FNI *pfni, int32_t *psid = pvNil);
};

#endif
