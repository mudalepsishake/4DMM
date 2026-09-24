/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    DOCPIC methods.

***************************************************************************/
#include "ched.h"
ASSERTNAME

RTCLASS(DOCPIC)
RTCLASS(DCPIC)

/** 3DMMv1.0: *************************************************************************
    Constructor for picture document.
***************************************************************************/
DOCPIC::DOCPIC(PDOCB pdocb, PCFL pcfl, CTG ctg, CNO cno) : DOCE(pdocb, pcfl, ctg, cno)
{
    _ppic = pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Destructor for picture document.
***************************************************************************/
DOCPIC::~DOCPIC(void)
{
    ReleasePpo(&_ppic);
}

/** 3DMMv1.0: *************************************************************************
    Static method to create a new picture document.
***************************************************************************/
PDOCPIC DOCPIC::PdocpicNew(PDOCB pdocb, PCFL pcfl, CTG ctg, CNO cno)
{
    DOCPIC *pdocpic;

    if (pvNil == (pdocpic = NewObj DOCPIC(pdocb, pcfl, ctg, cno)))
        return pvNil;
    if (!pdocpic->_FInit())
    {
        ReleasePpo(&pdocpic);
        return pvNil;
    }
    AssertPo(pdocpic, 0);
    return pdocpic;
}

/** 3DMMv1.0: *************************************************************************
    Create a new display gob for the document.
***************************************************************************/
PDDG DOCPIC::PddgNew(PGCB pgcb)
{
    return DCPIC::PdcpicNew(this, _ppic, pgcb);
}

/** 3DMMv1.0: *************************************************************************
    Return the size of the thing on file.
***************************************************************************/
int32_t DOCPIC::_CbOnFile(void)
{
    return _ppic->CbOnFile();
}

/** 3DMMv1.0: *************************************************************************
    Write the data out.
***************************************************************************/
bool DOCPIC::_FWrite(PBLCK pblck, bool fRedirect)
{
    AssertThis(0);
    AssertPo(pblck, 0);

    return _ppic->FWrite(pblck);
}

/** 3DMMv1.0: *************************************************************************
    Read the PIC.
***************************************************************************/
bool DOCPIC::_FRead(PBLCK pblck)
{
    Assert(_ppic == pvNil, "losing existing PIC");
    AssertPo(pblck, 0);

    _ppic = PIC::PpicRead(pblck);
    return _ppic != pvNil;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a DOCPIC.
***************************************************************************/
void DOCPIC::AssertValid(uint32_t grf)
{
    DOCPIC_PAR::AssertValid(0);
    AssertPo(_ppic, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the DOCPIC.
***************************************************************************/
void DOCPIC::MarkMem(void)
{
    AssertValid(0);
    DOCPIC_PAR::MarkMem();
    MarkMemObj(_ppic);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Constructor for a pic display gob.
***************************************************************************/
DCPIC::DCPIC(PDOCB pdocb, PPIC ppic, PGCB pgcb) : DDG(pdocb, pgcb)
{
    _ppic = ppic;
}

/** 3DMMv1.0: *************************************************************************
    Get the min-max for a DCPIC.
***************************************************************************/
void DCPIC::GetMinMax(RC *prcMinMax)
{
    prcMinMax->Set(20, 20, kswMax, kswMax);
}

/** 3DMMv1.0: *************************************************************************
    Static method to create a new DCPIC.
***************************************************************************/
PDCPIC DCPIC::PdcpicNew(PDOCB pdocb, PPIC ppic, PGCB pgcb)
{
    PDCPIC pdcpic;

    if (pvNil == (pdcpic = NewObj DCPIC(pdocb, ppic, pgcb)))
        return pvNil;

    if (!pdcpic->_FInit())
    {
        ReleasePpo(&pdcpic);
        return pvNil;
    }
    pdcpic->Activate(fTrue);

    AssertPo(pdcpic, 0);
    return pdcpic;
}

/** 3DMMv1.0: *************************************************************************
    Draw the picture.
***************************************************************************/
void DCPIC::Draw(PGNV pgnv, RC *prcClip)
{
    RC rc, rcSrc;

    pgnv->GetRcSrc(&rcSrc);
    pgnv->FillRc(&rcSrc, kacrWhite);
    _ppic->GetRc(&rc);
    rcSrc.Inset(5, 5);
    rc.StretchToRc(&rcSrc);
    pgnv->DrawPic(_ppic, &rc);
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a DCPIC.
***************************************************************************/
void DCPIC::AssertValid(uint32_t grf)
{
    DCPIC_PAR::AssertValid(0);
    AssertPo(_ppic, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the DCPIC.
***************************************************************************/
void DCPIC::MarkMem(void)
{
    AssertValid(0);
    DCPIC_PAR::MarkMem();
    MarkMemObj(_ppic);
}
#endif // 3DMMv1.0: DEBUG
