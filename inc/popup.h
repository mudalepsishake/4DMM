/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    popup.h: Popup menu classes

    Primary Author: ******
             MPFNT: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> CMH ---> GOB ---> GOK ---> BRWD ---> BRWL ---> MP
                                          |
                                          +------> BRWT ---> MPFNT

***************************************************************************/
#ifndef POPUP_H
#define POPUP_H

/** 3DMMv1.0: **********************************
    MP - Generic popup menu class
*************************************/
#define MP_PAR BRWL
#define kclsMP KLCONST2('M', 'P')
typedef class MP *PMP;
class MP : public MP_PAR
{
    ASSERT
    MARKMEM
    RTCLASS_DEC
    CMD_MAP_DEC(MP)

  protected:
    int32_t _cid; // 3DMMv1.0: cid to enqueue to apply selection
    PCMH _pcmh;   // 3DMMv1.0: command handler to enqueue command to

  protected:
    virtual void _ApplySelection(int32_t ithumSelect, int32_t sid) override;
    virtual int32_t _IthumFromThum(int32_t thumSelect, int32_t sidSelect) override;
    MP(PGCB pgcb) : MP_PAR(pgcb)
    {
    }
    bool _FInit(PRCA prca);

  public:
    ~MP(void);
    static PMP PmpNew(int32_t kidParent, int32_t kidMenu, PRCA prca, PCMD pcmd, BWS bws, int32_t ithumSelect,
                      int32_t sidSelect, CKI ckiRoot, CTG ctg, PCMH pcmh, int32_t cid, bool fMoveTop);

    virtual bool FCmdSelIdle(PCMD pcmd) override;
};

/** 3DMMv1.0: **********************************
    MPFNT - Font popup menu class
*************************************/
#define MPFNT_PAR BRWT
#define kclsMPFNT KLCONST4('m', 'p', 'f', 't')
typedef class MPFNT *PMPFNT;
class MPFNT : public MPFNT_PAR
{
    ASSERT
    MARKMEM
    RTCLASS_DEC
    CMD_MAP_DEC(MPFNT)

  protected:
    void _AdjustRc(int32_t cthum, int32_t cfrm);

    virtual void _ApplySelection(int32_t ithumSelect, int32_t sid) override;
    virtual bool _FSetThumFrame(int32_t istn, PGOB pgobPar) override;
    MPFNT(PGCB pgcb) : MPFNT_PAR(pgcb)
    {
    }

  public:
    ~MPFNT(void);
    static PMPFNT PmpfntNew(PRCA prca, int32_t kidParent, int32_t kidMenu, PCMD pcmd, int32_t ithumSelect, PGST pgst);

    virtual bool FCmdSelIdle(PCMD pcmd) override;
};

#endif // 3DMMv1.0: POPUP_H
