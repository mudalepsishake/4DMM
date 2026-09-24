/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Script interpreter for the gob based scripts.

***************************************************************************/
#ifndef SCREXEG_H
#define SCREXEG_H

/** 3DMMv1.0: **************************************
    Gob based script interpreter
****************************************/
typedef class SCEG *PSCEG;
#define SCEG_PAR SCEB
#define kclsSCEG KLCONST4('S', 'C', 'E', 'G')
class SCEG : public SCEG_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    // 3DMMv1.0: CAUTION: _pgob may be nil (even if the gob still exists)! Always access
    // 3DMMv1.0: thru _PgobThis.  When something is done that may cause the gob to be
    // 3DMMv1.0: freed (such as calling another script), set this to nil.
    PGOB _pgob;
    int32_t _hid;  // 3DMMv1.0: the handler id of the initialization gob
    int32_t _grid; // 3DMMv1.0: the unique gob run-time id of the initialization gob
    PWOKS _pwoks;  // 3DMMv1.0: the kidspace world this script belongs to

    virtual PGOB _PgobThis(void);
    virtual PGOB _PgobFromHid(int32_t hid);

    virtual bool _FExecOp(int32_t op) override;
    virtual PGL *_PpglrtvmThis(void) override;
    virtual PGL *_PpglrtvmGlobal(void) override;
    virtual PGL *_PpglrtvmRemote(int32_t lw) override;

    virtual int16_t _SwCur(void) override;
    virtual int16_t _SwMin(void) override;

    void _DoAlert(int32_t op);
    void _SetColorTable(CHID chid);
    void _DoEditControl(int32_t hid, int32_t stid, bool fGet);
    PGL _PglclrGet(CNO cno);
    bool _FLaunch(int32_t stid);

  public:
    SCEG(PWOKS pwoks, PRCA prca, PGOB pgob);

    void GobMayDie(void)
    {
        _pgob = pvNil;
    }
    virtual bool FResume(int32_t *plwReturn = pvNil, bool *pfPaused = pvNil) override;
};

// 3DMMv1.0: a Chunky resource reader for a color table
bool FReadColorTable(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);

#endif //! 3DMMv1.0: SCREXEG_H
