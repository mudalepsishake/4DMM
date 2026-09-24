/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    esl.h: Easel classes

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> CMH ---> GOB ---> GOK ---> ESL (generic easel)
                                          |
                                          +---> ESLT (text easel)
                                          |
                                          +---> ESLC (costume easel)
                                          |
                                          +---> ESLL (listener easel)
                                          |
                                          +---> ESLR (sound recording easel)

***************************************************************************/
#ifndef ESL_H
#define ESL_H

// 3DMMv1.0: Function to build a GCB to construct a child under a parent
bool FBuildGcb(PGCB pgcb, int32_t kidParent, int32_t kidChild);

// 3DMMv1.0: Function to set a GOK to a different state
void SetGokState(int32_t kid, int32_t st);

/** 3DMMv1.0: ***************************
    The generic easel class
*****************************/
typedef class ESL *PESL;
#define ESL_PAR GOK
#define kclsESL KLCONST3('E', 'S', 'L')
class ESL : public ESL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ESL)

  protected:
    ESL(PGCB pgcb) : GOK(pgcb)
    {
    }
    bool _FInit(PRCA prca, int32_t kidEasel);
    virtual bool _FAcceptChanges(bool *pfDismissEasel)
    {
        return fTrue;
    }

  public:
    static PESL PeslNew(PRCA prca, int32_t kidParent, int32_t hidEasel);
    ~ESL(void);

    bool FCmdDismiss(PCMD pcmd); // 3DMMv1.0: Handles both OK and Cancel
};

typedef class ESLT *PESLT; // 3DMMv1.0: SNE needs this
/** 3DMMv1.0: **************************************
    Spletter Name Editor class.  It's
    derived from EDSL, which is a Kauai
    single-line edit control
****************************************/
typedef class SNE *PSNE;
#define SNE_PAR EDSL
#define kclsSNE KLCONST3('S', 'N', 'E')
class SNE : public SNE_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PESLT _peslt; // 3DMMv1.0: easel to notify when text changes

  protected:
    SNE(PEDPAR pedpar) : EDSL(pedpar)
    {
    }

  public:
    static PSNE PsneNew(PEDPAR pedpar, PESLT peslt, PSTN pstnInit);
    virtual bool FReplace(const achar *prgch, int32_t cchIns, int32_t ich1, int32_t ich2, int32_t gin) override;
};

/** 3DMMv1.0: **************************************
    The text easel class
****************************************/
typedef class ESLT *PESLT;
#define ESLT_PAR ESL
#define kclsESLT KLCONST4('E', 'S', 'L', 'T')
class ESLT : public ESLT_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ESLT)

  protected:
    PMVIE _pmvie; // 3DMMv1.0: Movie that this TDT is in
    PACTR _pactr; // 3DMMv1.0: Actor of this TDT, or pvNil for new TDT
    PAPE _pape;   // 3DMMv1.0: Actor Preview Entity
    PSNE _psne;   // 3DMMv1.0: Spletter Name Editor
    PRCA _prca;   // 3DMMv1.0: Resource source for cursors
    PSFL _psflMtrl;
    PBCL _pbclMtrl;
    PSFL _psflTdf;
    PBCL _pbclTdf;
    PSFL _psflTdts;

  protected:
    ESLT(PGCB pgcb) : ESL(pgcb)
    {
    }
    bool _FInit(PRCA prca, int32_t kidEasel, PMVIE pmvie, PACTR pactr, PSTN pstnNew, int32_t tdtsNew, PTAG ptagTdfNew);
    virtual bool _FAcceptChanges(bool *pfDismissEasel) override;

  public:
    static PESLT PesltNew(PRCA prca, PMVIE pmvie, PACTR pactr, PSTN pstnNew = pvNil, int32_t tdtsNew = tdtsNil,
                          PTAG ptagTdfNew = pvNil);
    ~ESLT(void);

    bool FCmdRotate(PCMD pcmd);
    bool FCmdTransmogrify(PCMD pcmd);
    bool FCmdStartPopup(PCMD pcmd);
    bool FCmdSetFont(PCMD pcmd);
    bool FCmdSetShape(PCMD pcmd);
    bool FCmdSetColor(PCMD pcmd);
    bool FCmdPickColor(PCMD pcmd);

    bool FTextChanged(PSTN pstn);
};

/** 3DMMv1.0: ******************************************
    The actor easel (costume changer) class
********************************************/
typedef class ESLA *PESLA;
#define ESLA_PAR ESL
#define kclsESLA KLCONST4('E', 'S', 'L', 'A')
class ESLA : public ESLA_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ESLA)

  protected:
    PMVIE _pmvie; // 3DMMv1.0: Movie that this actor is in
    PACTR _pactr; // 3DMMv1.0: The actor that is being edited
    PAPE _pape;   // 3DMMv1.0: Actor Preview Entity
    PEDSL _pedsl; // 3DMMv1.0: Single-line edit control (for actor's name)

  protected:
    ESLA(PGCB pgcb) : ESL(pgcb)
    {
    }
    bool _FInit(PRCA prca, int32_t kidEasel, PMVIE pmvie, PACTR pactr);
    virtual bool _FAcceptChanges(bool *pfDismissEasel) override;

  public:
    static PESLA PeslaNew(PRCA prca, PMVIE pmvie, PACTR pactr);
    ~ESLA(void);

    bool FCmdRotate(PCMD pcmd);
    bool FCmdTool(PCMD pcmd);
    bool FCmdPickColor(PCMD pcmd);
};

/** 3DMMv1.0: **************************************
    Listener sound class
****************************************/
typedef class LSND *PLSND;
#define LSND_PAR BASE
#define kclsLSND KLCONST4('L', 'S', 'N', 'D')
class LSND : public LSND_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PGL _pgltag;         // 3DMMv1.0: PGL in case of chained sounds
    int32_t _vlm;        // 3DMMv1.0: Initial volume
    int32_t _vlmNew;     // 3DMMv1.0: User can redefine with slider
    bool _fLoop;         // 3DMMv1.0: Looping sound
    int32_t _objID;      // 3DMMv1.0: Owner's object ID
    int32_t _sty;        // 3DMMv1.0: Sound type
    int32_t _kidVol;     // 3DMMv1.0: Kid of volume slider
    int32_t _kidIcon;    // 3DMMv1.0: Kid of sound-type icon
    int32_t _kidEditBox; // 3DMMv1.0: Kid of sound-name box
    bool _fMatcher;      // 3DMMv1.0: Whether this is a motion-matched sound

  public:
    LSND(void)
    {
        _pgltag = pvNil;
    }
    ~LSND(void);

    bool FInit(int32_t sty, int32_t kidVol, int32_t kidIcon, int32_t kidEditBox, PGL *ppgltag, int32_t vlm, bool fLoop,
               int32_t objID, bool fMatcher);
    bool FValidSnd(void);
    void SetVlmNew(int32_t vlmNew)
    {
        _vlmNew = vlmNew;
    }
    void Play(void);
    bool FChanged(int32_t *pvlmNew, bool *pfNuked);
};

/** 3DMMv1.0: **************************************
    The listener easel class
****************************************/
typedef class ESLL *PESLL;
#define ESLL_PAR ESL
#define kclsESLL KLCONST4('E', 'S', 'L', 'L')
class ESLL : public ESLL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ESLL)

  protected:
    PMVIE _pmvie; // 3DMMv1.0: Movie that these sounds are in
    PSCEN _pscen; // 3DMMv1.0: Scene that these sounds are in
    PACTR _pactr; // 3DMMv1.0: Actor that sounds are attached to (or pvNil)
    LSND _lsndSpeech;
    LSND _lsndSfx;
    LSND _lsndMidi;
    LSND _lsndSpeechMM;
    LSND _lsndSfxMM;

  protected:
    ESLL(PGCB pgcb) : ESL(pgcb)
    {
    }

    bool _FInit(PRCA prca, int32_t kidEasel, PMVIE pmvie, PACTR pactr);
    virtual bool _FAcceptChanges(bool *pfDismissEasel) override;

  public:
    static PESLL PesllNew(PRCA prca, PMVIE pmvie, PACTR pactr);
    ~ESLL(void);

    bool FCmdVlm(PCMD pcmd);
    bool FCmdPlay(PCMD pcmd);
};

/** 3DMMv1.0: **************************************
    The sound recording easel class
****************************************/
typedef class ESLR *PESLR;
#define ESLR_PAR ESL
#define kclsESLR KLCONST4('E', 'S', 'L', 'R')
class ESLR : public ESLR_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ESLR)

  protected:
    PMVIE _pmvie;         // 3DMMv1.0: The movie to insert sound into
    bool _fSpeech;        // 3DMMv1.0: Recording Speech or SFX?
    PEDSL _pedsl;         // 3DMMv1.0: Single-line edit control for sound name
    PSREC _psrec;         // 3DMMv1.0: Sound recording object
    CLOK _clok;           // 3DMMv1.0: Clock to limit sound length
    bool _fRecording;     // 3DMMv1.0: Are we recording right now?
    bool _fPlaying;       // 3DMMv1.0: Are we playing back the recording?
    uint32_t _tsStartRec; // 3DMMv1.0: Time at which we started recording

  protected:
    ESLR(PGCB pgcb) : ESL(pgcb), _clok(HidUnique())
    {
    }
    bool _FInit(PRCA prca, int32_t kidEasel, PMVIE pmvie, bool fSpeech, PSTN pstnNew);
    virtual bool _FAcceptChanges(bool *pfDismissEasel) override;
    void _UpdateMeter(void);

  public:
    static PESLR PeslrNew(PRCA prca, PMVIE pmvie, bool fSpeech, PSTN pstnNew);
    ~ESLR(void);

    bool FCmdRecord(PCMD pcmd);
    bool FCmdPlay(PCMD pcmd);
    bool FCmdUpdateMeter(PCMD pcmd);
};

#endif // 3DMMEx: ESL_H
