/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    tdf.h: Three-D Font class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BACO ---> TDF  (Three-D Font)

***************************************************************************/
#ifndef TDF_H
#define TDF_H

/** 3DMMv1.0: **************************************
    3-D Font class
****************************************/
typedef class TDF *PTDF;
#define TDF_PAR BACO
#define kclsTDF KLCONST3('T', 'D', 'F')
class TDF : public TDF_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    int32_t _cch; // 3DMMv1.0: count of chars
    BRS _dyrMax;  // 3DMMv1.0: max character height
    BRS *_prgdxr; // 3DMMv1.0: character widths
    BRS *_prgdyr; // 3DMMv1.0: character heights

  protected:
    TDF(void)
    {
    }
    bool _FInit(PBLCK pblck);

  public:
    static bool FReadTdf(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);
    ~TDF(void);

    // 3DMMv1.0: This authoring-only API creates a new TDF based on a set of models
    static bool FCreate(PCRF pcrf, PGL pglkid, STN *pstn, CKI *pckiTdf = pvNil);

    PMODL PmodlFetch(CHID chid);
    BRS DxrChar(int32_t ich);
    BRS DyrChar(int32_t ich);
    BRS DyrMax(void)
    {
        return _dyrMax;
    }
};

#endif // 3DMMv1.0: TDF_H
