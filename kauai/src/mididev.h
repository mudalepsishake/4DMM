/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    The midi player sound device.

***************************************************************************/
#ifndef MIDIDEV_H
#define MIDIDEV_H

/** 3DMMv1.0: *************************************************************************
    The midi player.
***************************************************************************/
typedef class MIDP *PMIDP;
#define MIDP_PAR SNDMQ
#define kclsMIDP KLCONST4('M', 'I', 'D', 'P')
class MIDP : public MIDP_PAR
{
    RTCLASS_DEC

  protected:
    MIDP(void);

    virtual PSNQUE _PsnqueNew(void) override;
    virtual void _Suspend(bool fSuspend) override;

  public:
    static PMIDP PmidpNew(void);
    ~MIDP(void);

    // 3DMMv1.0: inherited methods
    virtual void SetVlm(int32_t vlm) override;
    virtual int32_t VlmCur(void) override;
};

#endif //! 3DMMv1.0: MIDIDEV_H
