/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    tdt.h: Three-D Text class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BACO ---> TMPL ---> TDT  (Three-D Text)

***************************************************************************/
#ifndef TDT_H
#define TDT_H

// 3DMMv1.0: 3-D Text Shapes - the positions and orientations of the letters
enum
{
    tdtsNil = -1,
    tdtsNormal = 0,
    tdtsArchPositive,
    tdtsCircleY,
    tdtsLargeMiddle,
    tdtsArchNegative,
    tdtsArchZ,
    tdtsCircleZ,
    tdtsVertical,
    tdtsGrowRight,
    tdtsGrowLeft,
    tdtsLim
};

// 3DMMv1.0: 3-D Actions
enum
{
    tdaNil = -1,
    tdaRest = 0,
    tdaLetterRotX, // 3DMMv1.0: each letter rotates around its own X axis
    tdaLetterRotY,
    tdaLetterRotZ,
    tdaSwingX, // 3DMMv1.0: letters skew right, then left, then back to normal
    tdaSwingY,
    tdaSwingZ,
    tdaPulse,    // 3DMMv1.0: letters grow 10%, then shrink back to normal
    tdaWordRotX, // 3DMMv1.0: (rotate entire word around X axis)
    tdaWordRotY,
    tdaWordRotZ,
    tdaWave,    // 3DMMv1.0: a bump ripples through the letters
    tdaReveal,  // 3DMMv1.0: letters slowly grow from zero height
    tdaWalk,    // 3DMMv1.0: walk (word hops forward as if walking)
    tdaHop,     // 3DMMv1.0: letters hop up and down
    tdaStretch, // 3DMMv1.0: stretch in X
    tdaLim
};

/** 3DMMv1.0: **************************************
    3-D Text class
****************************************/
typedef class TDT *PTDT;
#define TDT_PAR TMPL
#define kclsTDT KLCONST3('T', 'D', 'T')
class TDT : public TDT_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    static PGST _pgstAction; // 3DMMv1.0: Action names

    int32_t _tdts;       // 3DMMv1.0: TDT shape
    TAG _tagTdf;         // 3DMMv1.0: Tag to Three-D Font
    PMTRL _pmtrlDefault; // 3DMMv1.0: MTRL for TDT's default costume
    PACTN _pactnCache;   // 3DMMv1.0: Last-used action
    int32_t _tdaCache;   // 3DMMv1.0: Action in pactnCache

    // actorlight28: cache the true-colour tessellated MODL for each character
    // position.  TDT action playback asks for the same glyph model every cel;
    // rebuilding and BrModelAdd'ing that generated mesh every frame was the
    // 636/frame model-preparation storm seen by the profiler.  Keep this cache
    // actor/TDT-local so no BRender model state is shared between actors.
    enum { kcmodlLitCache = 256 };
    PMODL _rgpmodlLitCache[kcmodlLitCache];

    void _ReleaseLitModelCache(void);

  protected:
    virtual bool _FInit(PCFL pcfl, CTG ctgTmpl, CNO cnoTmpl) override;
    bool _FInitLists(void);
    PGL _PglibactParBuild(void);
    PGL _PglibsetBuild(void);
    PGG _PggcmidBuild(void);
    PGL _Pglbmat34Build(int32_t tda);
    PGG _PggcelBuild(int32_t tda);
    virtual PACTN _PactnFetch(int32_t tda) override;
    PACTN _PactnBuild(int32_t tda);
    virtual PMODL _PmodlFetch(CHID chidModl) override;
    int32_t _CcelOfTda(int32_t tda);
    void _ApplyAction(BMAT34 *pbmat34, int32_t tda, int32_t ich, int32_t ccel, int32_t icel, BRS xrChar, BRS pdxrText);
    void _ApplyShape(BMAT34 *pbmat34, int32_t tdts, int32_t cch, int32_t ich, BRS xrChar, BRS dxrText, BRS yrChar,
                     BRS dyrMax, BRS dyrTotal);

  public:
    TDT(void);
    static bool FSetActionNames(PGST pgstAction);
#ifdef DEBUG
    static void MarkActionNames(void);
#endif

    static PTDT PtdtNew(PSTN pstn, int32_t tdts, PTAG ptagTdf);
    ~TDT(void);
    static PGL PgltagFetch(PCFL pcfl, CTG ctg, CNO cno, bool *pfError);
    PTDT PtdtDup(void);

    void GetInfo(PSTN pstn, int32_t *ptdts, PTAG ptagTdf);
    bool FChange(PSTN pstn, int32_t tdts = tdtsNil, PTAG ptagTdf = pvNil);
    bool FWrite(PCFL pcfl, CTG ctg, CNO *pcno);
    bool FAdjustBody(PBODY pbody);
    virtual bool FSetDefaultCost(PBODY pbody) override;
    virtual PCMTL PcmtlFetch(int32_t cmid) override;
    virtual bool FGetActnName(int32_t anid, PSTN pstn) override;
};

#endif // 3DMMv1.0: TDT_H
