/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

  tgob.cpp

  Author: Sean Selitrennikoff

  Date: June, 1995

  This file contains all functionality for a whole lite text-gob.

***************************************************************************/

#include "studio.h"

ASSERTNAME
RTCLASS(TGOB)

/** 3DMMv1.0: **************************************************
 *
 * Constructor for text gobs.
 *
 ****************************************************/
TGOB::TGOB(GCB *pgcb) : GOB(pgcb)
{
    _acrFore = kacrBlack;
    _acrBack = kacrClear;
    _tav = tavTop;
    _tah = tahCenter;
    _onn = vapp.OnnDefVariable();
    _dypFont = vapp.DypTextDef();
}

/** 3DMMv1.0: **************************************************
 *
 * Constructor for text gobs.
 *
 ****************************************************/
TGOB::TGOB(int32_t hid) : GOB(hid)
{
    _acrFore = kacrBlack;
    _acrBack = kacrClear;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Draw this text.
 *
 * Parameters:
 *	pgnv - The environment to write to.
 *  prcClip - The clipping rectangle.
 *
 * Returns:
 *  None.
 *
 ***************************************************************************/
void TGOB::Draw(PGNV pgnv, RC *prcClip)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);

    RC rc, rcText;
    int32_t xp, yp;
    STN stnDraw(_stn);

    // 3DMMv1.0: Use a temporary stn here for the displayed text. For example, the tgob
    // 3DMMv1.0: text may be 'hello', but the dispayed text may be 'hel..'. This means
    // 3DMMv1.0: any dots will be added to the displayed string if required every time
    // 3DMMv1.0: we pass through here. If we don't do this then the following would
    // 3DMMv1.0: happen. On the first pass through here can find that we need to add
    // 3DMMv1.0: the dots and left justify the text, (Eg 'hello' becomes 'hel..'). On
    // 3DMMv1.0: the next pass however, we would find that 'hel..' does indeed fit in the
    // 3DMMv1.0: tgob ok and we would centre justify it. The user would see the text shift.

    GetRc(&rc, cooLocal);

    /* 3DMMv1.0: REVIEW peted: what happens for right-justify?  Why do we have to do
        this at all?  Why doesn't Kauai just take an RC, and then left-, right-,
        or center-align as appropriate, depending on the "SetFontAlign"
        setting? */
    xp = (_tah != tahCenter) ? 0 : rc.Dxp() / 2;
    yp = (_tav != tavCenter) ? 0 : rc.Dyp() / 2;

    pgnv->SetOnn(_onn);
    pgnv->SetFontSize(_dypFont);

    // 3DMMv1.0: Center justify the text if it fits in the gob. Otherwise left justify it.
    pgnv->GetRcFromStn(&rcText, &stnDraw, 0, 0);

    if (rcText.Dxp() < rc.Dxp())
    {
        pgnv->SetFontAlign(_tah, _tav);
        pgnv->DrawStn(&stnDraw, xp, yp, _acrFore, _acrBack);
    }
    else
    {
        STN stnDots;
        RC rcDots;
        int iCh;

        stnDots.SetSz(PszLit(".."));
        pgnv->GetRcFromStn(&rcDots, &stnDots, 0, 0);

        for (iCh = stnDraw.Cch() - 1; iCh >= 0; --iCh)
        {
            stnDraw.Delete(iCh);
            stnDraw.FAppendStn(&stnDots);

            pgnv->GetRcFromStn(&rcText, &stnDraw, 0, 0);

            if (rcText.Dxp() < rc.Dxp())
            {
                pgnv->SetFontAlign(tahLeft, _tav);
                pgnv->DrawStn(&stnDraw, 0, yp, _acrFore, _acrBack);

                break;
            }
        }
    }
}

/** 3DMMv1.0: **************************************************
 *
 * Create Tgob's for any of the text based browsers
 * 		using the font specified by idsFont
 * Static routine
 * Parameters
 *		kidFrm = kid of text frame
 *		idsFont = string registry id
 *		tav = text align vertical
 *
 ****************************************************/
PTGOB TGOB::PtgobCreate(int32_t kidFrm, int32_t idsFont, int32_t tav, int32_t hid)
{
    RC rcRel;
    RC rcAbs;
    PGOB pgob;
    STN stn;
    GCB gcb;
    int32_t onn;
    PTGOB ptgob;

    rcRel.xpLeft = rcRel.ypTop = krelZero;
    rcRel.xpRight = rcRel.ypBottom = krelOne;
    rcAbs.Set(0, 0, 0, 0);

    pgob = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidFrm);

    if (pgob == pvNil)
        return pvNil;

    if (hidNil == hid)
        hid = GOB::HidUnique();
    gcb.Set(hid, pgob, fgobNil, kginDefault, &rcAbs, &rcRel);

    if (pvNil == (ptgob = NewObj TGOB(&gcb)))
        return pvNil;

    if (idsFont != idsNil)
    {
        PSTDIO pstdio;

        pstdio = (PSTDIO)vapp.Pkwa()->PgobFromCls(kclsSTDIO);
        Assert(pstdio != pvNil, "Creating a TGOB with no STDIO present");
        pstdio->GetStnMisc(idsFont, &stn);
        vapp.FGetOnn(&stn, &onn); // 3DMMv1.0:  Ignore failure
        if (onn != onnNil)
            ptgob->SetFont(onn);
    }
    else
        Assert(ptgob->_onn == vapp.OnnDefVariable(), "Someone else set the _onn?");

    ptgob->_tav = tav;
    return ptgob;
}

/** 3DMMv1.0: ****************************************************************************
    SetAlign
        set the text alignment for this TGOB

    Arguments:
        long tah  --  the horizontal alignment
        long tav  --  the vertical alignment
************************************************************ PETED ***********/
void TGOB::SetAlign(int32_t tah, int32_t tav)
{
    if (tah != tahLim)
        _tah = tah;
    if (tav != tavLim)
        _tav = tav;
}

/** 3DMMv1.0: ****************************************************************************
    GetAlign
        Gets the text alignment for this TGOB

    Arguments:
        long *ptah  --  pointer to take the horizontal alignment
        long *ptav  --  pointer to take the vertical alignment
************************************************************ PETED ***********/
void TGOB::GetAlign(int32_t *ptah, int32_t *ptav)
{
    if (ptah != pvNil)
        *ptah = _tah;
    if (ptav != pvNil)
        *ptav = _tav;
}

#ifdef DEBUG

/** 3DMMv1.0: ***************************************************************************
 *
 *	Mark memory used by the TGOB
 *
 *	Parameters:
 *		None.
 *
 *	Returns:
 *		Nothing.
 *
 *****************************************************************************/
void TGOB::MarkMem(void)
{
    AssertThis(0);

    TGOB_PAR::MarkMem();
}

/** 3DMMv1.0: ***************************************************************************\
 *
 *	Assert the validity of the TGOB
 *
 *	Parameters:
 *		grf - bit array of options.
 *
 *	Returns:
 *		Nothing.
 *
\*****************************************************************************/
void TGOB::AssertValid(uint32_t grf)
{
    TGOB_PAR::AssertValid(fobjAllocated);
}

#endif // 3DMMv1.0: DEBUG
