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
#ifndef MIDIDEV2_H
#define MIDIDEV2_H

typedef class MSMIX *PMSMIX;

/** 3DMMv1.0: *************************************************************************
    The midi player using a Midi stream.
***************************************************************************/
typedef class MDPS *PMDPS;
#define MDPS_PAR SNDMQ
#define kclsMDPS KLCONST4('M', 'D', 'P', 'S')
class MDPS : public MDPS_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PMSMIX _pmsmix;

    MDPS(void);

    virtual bool _FInit(void) override;
    virtual PSNQUE _PsnqueNew(void) override;
    virtual void _Suspend(bool fSuspend) override;

  public:
    static PMDPS PmdpsNew(void);
    ~MDPS(void);

    // 3DMMv1.0: inherited methods
    virtual void SetVlm(int32_t vlm) override;
    virtual int32_t VlmCur(void) override;
};

#endif //! 3DMMv1.0: MIDIDEV2_H
