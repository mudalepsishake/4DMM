/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: **************************************************************************

    DOCMBMP methods.

****************************************************************************/
#include "ched.h"
ASSERTNAME

RTCLASS(DOCMBMP)
RTCLASS(DCMBMP)

/** 3DMMv1.0: **************************************************************************
    Constructor for a MBMP document.
****************************************************************************/
DOCMBMP::DOCMBMP(PDOCB pdocb, PCFL pcfl, CTG ctg, CNO cno) : DOCE(pdocb, pcfl, ctg, cno)
{
    _pmbmp = pvNil;
}

/** 3DMMv1.0: **************************************************************************
    Destructor for a MBMP document.
****************************************************************************/
DOCMBMP::~DOCMBMP(void)
{
    ReleasePpo(&_pmbmp);
}

/** 3DMMv1.0: **************************************************************************
    Static method to create a new MBMP document.
****************************************************************************/
PDOCMBMP DOCMBMP::PdocmbmpNew(PDOCB pdocb, PCFL pcfl, CTG ctg, CNO cno)
{
    PDOCMBMP pdocmbmp;

    if (pvNil == (pdocmbmp = NewObj DOCMBMP(pdocb, pcfl, ctg, cno)))
        return pvNil;
    if (!pdocmbmp->_FInit())
    {
        ReleasePpo(&pdocmbmp);
        return pvNil;
    }
    AssertPo(pdocmbmp, 0);
    return pdocmbmp;
}

/** 3DMMv1.0: **************************************************************************
    Create a new display gob for the MBMP document.
****************************************************************************/
PDDG DOCMBMP::PddgNew(PGCB pgcb)
{
    return DCMBMP::PdcmbmpNew(this, _pmbmp, pgcb);
}

/** 3DMMv1.0: *************************************************************************
    Return the size of the thing on file.
***************************************************************************/
int32_t DOCMBMP::_CbOnFile(void)
{
    return _pmbmp->CbOnFile();
}

/** 3DMMv1.0: **************************************************************************
    Write the data out.
****************************************************************************/
bool DOCMBMP::_FWrite(PBLCK pblck, bool fRedirect)
{
    AssertThis(0);
    AssertPo(pblck, 0);

    return _pmbmp->FWrite(pblck);
}

/** 3DMMv1.0: ***************************************************************************
    Read the MBMP.
*****************************************************************************/
bool DOCMBMP::_FRead(PBLCK pblck)
{
    Assert(pvNil == _pmbmp, "losing existing MBMP");
    AssertPo(pblck, 0);

    _pmbmp = MBMP::PmbmpRead(pblck);
    return pvNil != _pmbmp;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a DOCMBMP.
***************************************************************************/
void DOCMBMP::AssertValid(uint32_t grf)
{
    DOCMBMP_PAR::AssertValid(0);
    AssertPo(_pmbmp, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the DOCMBMP.
***************************************************************************/
void DOCMBMP::MarkMem(void)
{
    AssertValid(0);
    DOCMBMP_PAR::MarkMem();
    MarkMemObj(_pmbmp);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: ***************************************************************************
    Constructor for a pic display gob.
*****************************************************************************/
DCMBMP::DCMBMP(PDOCB pdocb, PMBMP pmbmp, PGCB pgcb) : DDG(pdocb, pgcb)
{
    _pmbmp = pmbmp;
}

/** 3DMMv1.0: ***************************************************************************
    Get the min-max for a DCMBMP.
*****************************************************************************/
void DCMBMP::GetMinMax(RC *prcMinMax)
{
    prcMinMax->Set(0, 0, kswMax, kswMax);
}

/** 3DMMv1.0: ***************************************************************************
    Static method to create a new DCMBMP.
*****************************************************************************/
PDCMBMP DCMBMP::PdcmbmpNew(PDOCB pdocb, PMBMP pmbmp, PGCB pgcb)
{
    PDCMBMP pdcmbmp;

    if (pvNil == (pdcmbmp = NewObj DCMBMP(pdocb, pmbmp, pgcb)))
        return pvNil;

    if (!pdcmbmp->_FInit())
    {
        ReleasePpo(&pdcmbmp);
        return pvNil;
    }
    pdcmbmp->Activate(fTrue);

    AssertPo(pdcmbmp, 0);
    return pdcmbmp;
}

/** 3DMMv1.0: *************************************************************************
    Draw the MBMP.
***************************************************************************/
void DCMBMP::Draw(PGNV pgnv, RC *prcClip)
{
    RC rcMbmp, rcDdg;

    // 3DMMv1.0: retrieve appropriate rectangles
    GetRc(&rcDdg, cooLocal);
    _pmbmp->GetRc(&rcMbmp);
    rcMbmp.CenterOnRc(&rcDdg);

    // 3DMMv1.0: erase *prcClip
    pgnv->FillRc(prcClip, kacrWhite);
    pgnv->FillRcApt(&rcMbmp, &vaptLtGray, kacrLtGray, kacrWhite);

    // 3DMMv1.0: draw mbmp in GPT
    pgnv->DrawMbmp(_pmbmp, &rcMbmp);
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a DCMBMP.
***************************************************************************/
void DCMBMP::AssertValid(uint32_t grf)
{
    DCMBMP_PAR::AssertValid(0);
    AssertPo(_pmbmp, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the DCMBMP.
***************************************************************************/
void DCMBMP::MarkMem(void)
{
    AssertValid(0);
    DCMBMP_PAR::MarkMem();
    MarkMemObj(_pmbmp);
}
#endif // 3DMMv1.0: DEBUG
