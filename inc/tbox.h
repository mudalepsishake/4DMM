/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    Status: All changes must be code reviewed.

    Textbox Class

        Textbox (TBOX)

            TXRD ---> TBOX

    Drawing stuff

        Textbox border (TBXB)

            GOB  ---> TBXB

        Textbox Ddg (TBXG)

            TXRG ---> TBXG  (created as a child Gob of a TBXB)

    Cut/Copy/Paste Stuff

        Clipboard object (TCLP)

            DOCB ---> TCLP

***************************************************************************/

#ifndef TBOX_H
#define TBOX_H

//
// 3DMMv1.0: Defines for global text box constant values
//
#define kdzpBorderTbox 5                    // 3DMMv1.0: Width of the border in pixels
#define kdxpMinTbox 16 + 2 * kdxpIndentTxtg // 3DMMv1.0: Minimum Width of a tbox in pixels
#define kdypMinTbox 12                      // 3DMMv1.0: Minimum Height of a tbox in pixels
#define kxpDefaultTbox 177                  // 3DMMv1.0: Default location of a tbox
#define kypDefaultTbox 78                   // 3DMMv1.0: Default location of a tbox
#define kdxpDefaultTbox 140                 // 3DMMv1.0: Default width of a tbox
#define kdypDefaultTbox 100                 // 3DMMv1.0: Default height of a tbox

//
//
// 3DMMv1.0: The border for a single textbox (TBXB)
//
//

//
// 3DMMv1.0: Definitions for each of the anchor points in a border
//
enum TBXT
{
    tbxtUp,
    tbxtUpRight,
    tbxtRight,
    tbxtDownRight,
    tbxtDown,
    tbxtDownLeft,
    tbxtLeft,
    tbxtUpLeft,
    tbxtMove
};

#define TBXB_PAR GOB

typedef class TBXB *PTBXB;
#define kclsTBXB KLCONST4('T', 'B', 'X', 'B')
class TBXB : public TBXB_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    PTBOX _ptbox;         // 3DMMv1.0: Owning text box.
    bool _fTrackingMouse; // 3DMMv1.0: Are we tracking the mouse.
    TBXT _tbxt;           // 3DMMv1.0: The anchor point being dragged.
    int32_t _xpPrev;      // 3DMMv1.0: Previous x coord of the mouse.
    int32_t _ypPrev;      // 3DMMv1.0: Previous y coord of the mouse.
    RC _rcOrig;           // 3DMMv1.0: Original size of the border.

    TBXB(PTBOX ptbox, PGCB pgcb) : GOB(pgcb)
    {
        _ptbox = ptbox;
    }

    TBXT _TbxtAnchor(int32_t xp, int32_t yp); // 3DMMv1.0: Returns the anchor point the mouse is at.

  public:
    //
    // 3DMMv1.0: Creates a text box with border
    //
    static PTBXB PtbxbNew(PTBOX ptbox, PGCB pgcb);

    //
    // 3DMMv1.0: Overridden routines
    //
    void Draw(PGNV pgnv, RC *prcClip) override;
    virtual void Activate(bool fActive);
    virtual bool FPtIn(int32_t xp, int32_t yp) override;
    virtual bool FCmdMouseMove(PCMD_MOUSE pcmd) override;
    virtual bool FCmdTrackMouse(PCMD_MOUSE pcmd) override;

    void AttachToMouse(void);
};

//
//
// 3DMMv1.0: The DDG for a single textbox (TBXG).
//
//

#define TBXG_PAR TXRG

typedef class TBXG *PTBXG;
#define kclsTBXG KLCONST4('T', 'B', 'X', 'G')
class TBXG : public TBXG_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(TBXG)

  private:
    PTBXB _ptbxb; // 3DMMv1.0: Enclosing border.
    RC _rcOld;    // 3DMMv1.0: Old rectangle for the ddg.

    TBXG(PTXRD ptxrd, PGCB pgcb) : TXRG(ptxrd, pgcb)
    {
    }
    ~TBXG(void);

  public:
    //
    // 3DMMv1.0: Creation function
    //
    static PTBXG PtbxgNew(PTBOX ptbox, PGCB pgcb);

    //
    // 3DMMv1.0: Accessors
    //
    void SetTbxb(PTBXB ptbxb)
    {
        _ptbxb = ptbxb;
    }
    PTBXB Ptbxb(void)
    {
        return _ptbxb;
    }

    //
    // 3DMMv1.0: Scrolling
    //
    bool FNeedToScroll(void);     // 3DMMv1.0: Does this text box need to scroll anything
    void Scroll(int32_t scaVert); // 3DMMv1.0: Scrolls to beginning or a single pixel only.

    //
    // 3DMMv1.0: Overridden routines
    //
    virtual bool FPtIn(int32_t xp, int32_t yp) override;
    virtual bool FCmdMouseMove(PCMD_MOUSE pcmd) override;
    virtual bool FCmdTrackMouse(PCMD_MOUSE pcmd) override;
    virtual bool FCmdClip(PCMD pcmd) override;
    virtual bool FEnableDdgCmd(PCMD pcmd, uint32_t *pgrfeds) override;
    virtual void Draw(PGNV pgnv, RC *prcClip) override;
    virtual int32_t _DxpDoc(void) override;
    virtual void _NewRc(void) override;
    virtual void InvalCp(int32_t cp, int32_t ccpIns, int32_t ccpDel) override;
    virtual void Activate(bool fActive) override;
    virtual void _FetchChp(int32_t cp, PCHP pchp, int32_t *pcpMin = pvNil, int32_t *pcpLim = pvNil) override;

    //
    // 3DMMv1.0: Status
    //
    bool FTextSelected(void);

    //
    // 3DMMv1.0: Only for TBXB
    //
    bool _FDoClip(int32_t tool); // 3DMMv1.0: Actually does a clipboard command.
};

enum
{
    grfchpNil = 0,
    kfchpOnn = 0x01,
    kfchpDypFont = 0x02,
    kfchpBold = 0x04,
    kfchpItalic = 0x08
};
const uint32_t kgrfchpAll = (kfchpOnn | kfchpDypFont | kfchpBold | kfchpItalic);

//
//
// 3DMMv1.0: Text box document class (TBOX).
//
//
typedef class TBOX *PTBOX;

#define TBOX_PAR TXRD
#define kclsTBOX KLCONST4('T', 'B', 'O', 'X')
class TBOX : public TBOX_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    PSCEN _pscen;       // 3DMMv1.0: The owning scene
    int32_t _nfrmFirst; // 3DMMv1.0: Frame the tbox appears in.
    int32_t _nfrmMax;   // 3DMMv1.0: Frame the tbox disappears in.
    int32_t _nfrmCur;   // 3DMMv1.0: Current frame number.
    bool _fSel;         // 3DMMv1.0: Is this tbox selected?
    bool _fStory;       // 3DMMv1.0: Is this a story text box.
    RC _rc;             // 3DMMv1.0: Size of text box.

    TBOX(void) : TXRD()
    {
    }

  public:
    //
    // 3DMMv1.0: Creation routines
    //
    static PTBOX PtboxNew(PSCEN pscen = pvNil, RC *prcRel = pvNil, bool fStory = fTrue);
    PDDG PddgNew(PGCB pgcb) override
    {
        return TBXG::PtbxgNew(this, pgcb);
    }
    static PTBOX PtboxRead(PCRF pcrf, CNO cno, PSCEN pscen);
    bool FWrite(PCFL pcfl, CNO cno);
    bool FDup(PTBOX *pptbox);

    //
    // 3DMMv1.0: Movie specific functions
    //
    void SetScen(PSCEN pscen);
    bool FIsVisible(void);
    bool FGotoFrame(int32_t nfrm);
    void InsertDuplicateFramesAfter(int32_t nfrm, int32_t cfrm);
    void Select(bool fSel);
    bool FSelected(void)
    {
        return _fSel;
    }
    bool FGetLifetime(int32_t *pnfrmStart, int32_t *pnfrmLast);
    bool FShowCore(void);
    bool FShow(void);
    void HideCore(void);
    bool FHide(void);
    bool FStory(void)
    {
        return _fStory;
    }
    void SetTypeCore(bool fStory);
    bool FSetType(bool fStory);
    bool FNeedToScroll(void);
    void Scroll(void);
    PSCEN Pscen(void)
    {
        return _pscen;
    }
    bool FTextSelected(void);
    bool FSetAcrBack(ACR acr);
    bool FSetAcrText(ACR acr);
    bool FSetOnnText(int32_t onn);
    bool FSetDypFontText(int32_t dypFont);
    bool FSetStyleText(uint32_t grfont);
    void SetStartFrame(int32_t nfrm);
    void SetOnnDef(int32_t onn)
    {
        _onnDef = onn;
    }
    void SetDypFontDef(int32_t dypFont)
    {
        _dypFontDef = dypFont;
    }
    void FetchChpSel(PCHP pchp, uint32_t *pgrfchp);
    void AttachToMouse(void);

    //
    // 3DMMv1.0: Overridden functions
    //
    void SetDirty(bool fDirty = fTrue) override;
    virtual bool FAddUndo(PUNDB pundb) override;
    virtual void ClearUndo(void) override;
    void ParClearUndo(void)
    {
        TBOX_PAR::ClearUndo();
    }

    //
    // 3DMMv1.0: TBXG/TBXB specific funtions
    //
    void GetRc(RC *prc)
    {
        *prc = _rc;
    }
    void SetRc(RC *prc);
    void CleanDdg(void);
    int32_t Itbox(void);

    //
    // 3DMMv1.0: Undo access functions, not for use by anyone but tbox.cpp
    //
    int32_t NfrmFirst(void)
    {
        return _nfrmFirst;
    }
    int32_t nfrmMax(void)
    {
        return _nfrmMax;
    }
};

//
//
// 3DMMv1.0: Textbox document for clipping
//
//
typedef class TCLP *PTCLP;

#define TCLP_PAR DOCB
#define kclsTCLP KLCONST4('T', 'C', 'L', 'P')
class TCLP : public TCLP_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    PTBOX _ptbox; // 3DMMv1.0: Text box copy.
    TCLP(void)
    {
    }

  public:
    //
    // 3DMMv1.0: Constructors and destructors
    //
    static PTCLP PtclpNew(PTBOX ptbox);
    ~TCLP(void);

    //
    // 3DMMv1.0: Pasting
    //
    bool FPaste(PSCEN pscen);
};

#endif
