/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    Chunky editor main header file

***************************************************************************/
#ifndef CHED_H
#define CHED_H

#include "kidframe.h"
#include "chedres.h"

// 3DMMv1.0: creator type for the chunky editor
#define kctgChed KLCONST4('C', 'H', 'E', 'D')

#include "chdoc.h"

#define APP_PAR APPB
#define kclsAPP KLCONST3('A', 'P', 'P')
class APP : public APP_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(APP)

  protected:
    virtual bool _FInit(uint32_t grfapp, uint32_t grfgob, int32_t ginDef) override;
    virtual void _FastUpdate(PGOB pgob, PREGN pregnClip, uint32_t grfapp = fappNil, PGPT pgpt = pvNil) override;

  public:
    virtual void GetStnAppName(PSTN pstn) override;
    virtual void UpdateHwnd(KWND hwnd, RC *prc, uint32_t grfapp = fappNil) override;

    virtual bool FCmdOpen(PCMD pcmd);
};

#endif //! 3DMMv1.0: CHED_H
