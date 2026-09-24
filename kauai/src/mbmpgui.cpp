/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Masked bitmap stuff that a GUI app might need.

***************************************************************************/
#include "frame.h"
ASSERTNAME

/** 3DMMv1.0: *************************************************************************
    This routine is called to draw the masked bitmap onto prgbPixels.
    cbRow and dyp are the respective width and height of prgbPixels.
    xpRef and ypRef are the coordinates within the prgbPixels to place
    the reference point of the MBMP.  The drawing will be clipped to both
    prcClip and pregnClip, which may be nil.
***************************************************************************/
void MBMP::Draw(uint8_t *prgbPixels, int32_t cbRow, int32_t dyp, int32_t xpRef, int32_t ypRef, RC *prcClip,
                PREGN pregnClip)
{
    AssertThis(0);
    AssertIn(cbRow, 1, kcbMax);
    AssertIn(dyp, 1, kcbMax);
    AssertPvCb(prgbPixels, LwMul(cbRow, dyp));
    AssertNilOrVarMem(prcClip);
    AssertNilOrPo(pregnClip, 0);

#ifdef IN_80386
    // 3DMMv1.0: NOTE the "rep movsd" and "rep movsb" used for writing the bytes is
    // 3DMMv1.0: responsible for most of the speed improvement of this over the straight
    // 3DMMv1.0: C code.  The remaining asm gives us an additional 5 - 15 percent depending
    // 3DMMv1.0: on the complexity of the transparency (the more complex, the more
    // 3DMMv1.0: improvement over the C version).

    int32_t yp, dxpT, xpOn;
    uint8_t *qbRowSrc, *qbLastSrc, *pbOff;
    int16_t *qcb;
    uint8_t bFill;
    int32_t lwFill;
    REGSC regsc;
    RC rcClip(0, 0, cbRow, dyp);
    MBMPH *qmbmph = _Qmbmph();
    RC rcMbmp = qmbmph->rc;
    bool fMask = qmbmph->fMask;

    // 3DMMv1.0: Intersect prcClip with the boundary of prgbPixels.
    if (pvNil != prcClip && !rcClip.FIntersect(prcClip))
        return;

    // 3DMMv1.0: Translate the mbmp's rc into coordinate system of prgbPixel's rc
    rcMbmp.Offset(xpRef, ypRef);

    // 3DMMv1.0: Intersect rcClip with mbmp's translated rc.
    if (!rcClip.FIntersect(&rcMbmp))
        return;

    // 3DMMv1.0: Set up the region scanner
    if (pvNil != pregnClip)
        regsc.Init(pregnClip, &rcClip);
    else
        regsc.InitRc(&rcClip, &rcClip);

    qcb = _Qrgcb();
    qbRowSrc = (uint8_t *)PvAddBv(qcb, _cbRgcb);
    prgbPixels += LwMul(rcClip.ypTop, cbRow) + rcClip.xpLeft;
    rcMbmp.Offset(-rcClip.xpLeft, -rcClip.ypTop);
    rcClip.OffsetToOrigin();

    if (fMask)
    {
        bFill = qmbmph->bFill;
        lwFill = LwFromBytes(bFill, bFill, bFill, bFill);
    }

    // 3DMMv1.0: Step down through rgcb until to top of clipping rc
    for (yp = rcMbmp.ypTop; yp < 0; yp++)
        qbRowSrc += *qcb++;

    // 3DMMv1.0: copy each row appropriately
    for (;;)
    {
        if ((xpOn = regsc.XpCur()) == klwMax)
        {
            // 3DMMv1.0: empty strip of the region
            int32_t dypT;

            dypT = regsc.DypCur();
            if ((yp += dypT) >= rcClip.ypBottom)
                break;
            regsc.ScanNext(dypT);
            prgbPixels += LwMul(dypT, cbRow);
            for (; dypT > 0; dypT--)
                qbRowSrc += *qcb++;
            continue;
        }
        AssertIn(xpOn, 0, rcClip.xpRight);
        pbOff = prgbPixels + LwMin(regsc.XpFetch(), rcClip.xpRight);

        // 3DMMv1.0: register allocation:
        // 3DMMv1.0: esi: qbSrc
        // 3DMMv1.0: edi: pbDst
        // 3DMMv1.0: ebx: pbOn
        // 3DMMv1.0: edx: dxp
        // 3DMMv1.0: eax: temp value

#define qbSrc esi
#define pbDst edi
#define pbOn ebx
#define dxp edx
#define lwT eax

        __asm {
            // 3DMMv1.0: qbSrc = qbRowSrc;
			mov		qbSrc,qbRowSrc

                // 3DMMv1.0: lwT = *qcb++;
			mov		lwT,qcb
			add		qcb,2
			movzx	lwT,WORD PTR[lwT]

            // 3DMMv1.0: qbLastSrc = qbSrc + lwT
			lea		lwT,DWORD PTR[qbSrc+lwT-1]
			mov		qbLastSrc,lwT

                // 3DMMv1.0: pbDst = prgbPixels + rcMbmp.xpLeft;
                // 3DMMv1.0: pbOn = prgbPixels + xpOn;
			mov		pbOn,xpOn
			mov		pbDst,prgbPixels
			add		pbOn,pbDst
			add		pbDst,rcMbmp.xpLeft

                // 3DMMv1.0: dxp = 0;
			xor		dxp,dxp

LGetDxp:
                // 3DMMv1.0: if (qbLastSrc <= qbSrc) goto LNextRow
			cmp		qbLastSrc,qbSrc
			jbe		LNextRow

                // 3DMMv1.0: dxp = qbSrc[1]; pbDst += *qbSrc; qbSrc += 2;
			movzx	lwT,BYTE PTR[qbSrc]
			movzx	dxp,BYTE PTR[qbSrc+1]
			add		pbDst,lwT
			add		qbSrc,2

LTestDxp:
                // 3DMMv1.0: if (dxp == 0) goto LGetDxp
			test	dxp,dxp
			je		LGetDxp

                // 3DMMv1.0: if (pbDst + dxp > pbOn) goto LAfterOn;
			lea		lwT,DWORD PTR[pbDst+dxp]
			cmp		lwT,pbOn
			ja		LAfterOn

                // 3DMMv1.0: pbDst += dxp;
			add		pbDst,dxp

                // 3DMMv1.0: if (!fMask) qbSrc += dxp; goto LGetDxp
			test	fMask,-1L
			jnz		LGetDxp
			add		qbSrc,dxp
			jmp		LGetDxp

LAfterOn:
                // 3DMMv1.0: Assert(pbDst + dxp > pbOn, 0);
                // 3DMMv1.0: if (pbDst < pbOff) goto LBeforeOff;
			cmp		pbDst,pbOff
			jb		LBeforeOff

                // 3DMMv1.0: save the value of dxp across C code
			mov		dxpT,dxp
        }

        // 3DMMv1.0: destination is after pbOff - need to advance the region scan
        xpOn = regsc.XpFetch();
        if (xpOn == klwMax)
            goto LNextRow;
        pbOff = prgbPixels + LwMin(regsc.XpFetch(), rcClip.xpRight);

        __asm {
            // 3DMMv1.0: restore the value of dxp
			mov		dxp,dxpT

                // 3DMMv1.0: pbOn = prgbPixels + xpOn;
			mov		pbOn,prgbPixels
			add		pbOn,xpOn

                // 3DMMv1.0: goto LTestDxp;
			jmp		LTestDxp

LBeforeOff:
                // 3DMMv1.0: AssertIn(0, pbOn - pbDst - dxp + 1, pbOff - pbDst);
                // 3DMMv1.0: if (pbOn <= pbDst) goto LDstAfterOn;
			cmp		pbOn,pbDst
			jbe		LDstAfterOn

                // 3DMMv1.0: destination is before pbOn: use pbDst as a temporary value to
                // 3DMMv1.0: store (pbDst - pbOn), which should be negative.
                //
                // 3DMMv1.0: pbDst -= pbOn;
                // 3DMMv1.0: dxp += pbDst;
			sub		pbDst,pbOn
			add		dxp,pbDst

                // 3DMMv1.0: if (!fMask) qbSrc -= pbDst;
			test	fMask,-1L
			jnz		LMask
			sub		qbSrc,pbDst

LMask:
                // 3DMMv1.0: pbDst = pbOn;
			mov		pbDst,pbOn

LDstAfterOn:
                // 3DMMv1.0: AssertIn(0, pbOn - pbDst, pbOff - pbDst);
                // 3DMMv1.0: lwT = LwMin(dxp, pbOff - pbDst);
			mov		lwT,pbOff
			sub		lwT,pbDst
			cmp		lwT,dxp
			jle		LKeepLwT
			mov		lwT,dxp

LKeepLwT:
                // 3DMMv1.0: dxp -= lwT;
			sub		dxp,lwT

                // 3DMMv1.0: if (fMask) goto LFill;
			test	fMask,-1L
			jnz		LFill

                // 3DMMv1.0: CopyPb(qbSrc, pbDst, lwT);
                // 3DMMv1.0: qbSrc += lwT; pbDst += lwT;

                // 3DMMv1.0: move the longs
			mov		ecx,lwT
			shr		ecx,2
			rep		movsd

                // 3DMMv1.0: move the extra bytes
			mov		ecx,lwT
			and		ecx,3
			rep		movsb

                // 3DMMv1.0: goto LTestDxp;
			jmp		LTestDxp

LFill:
                // 3DMMv1.0: FillPb(pbDst, lwT, _bFill);
                // 3DMMv1.0: pbDst += lwT;
			mov		ecx,lwT
			mov		eax,lwFill

                // 3DMMv1.0: fill the longs
			mov		dxpT,ecx
			shr		ecx,2
			rep		stosd

                // 3DMMv1.0: fill the bytes
			mov		ecx,dxpT
			and		ecx,3
			rep		stosb

                // 3DMMv1.0: goto LTestDxp;
			jmp		LTestDxp
        }

#undef qbSrc
#undef pbDst
#undef pbOn
#undef dxp
#undef lwT

    LNextRow:
        if (++yp >= rcClip.ypBottom)
            break;
        regsc.ScanNext(1);

        // 3DMMv1.0: advance row pointers
        prgbPixels += cbRow;
        qbRowSrc = qbLastSrc + 1;
    }

#else //! 3DMMv1.0: IN_80386

    int32_t yp, dxp, dypT, dxpT;
    uint8_t *qbRowSrc, *qbSrc, *qbLastSrc;
    uint8_t *pbOn, *pbOff, *pbDst;
    int16_t *qcb;
    REGSC regsc;
    RC rcClip(0, 0, cbRow, dyp);
    MBMPH *qmbmph = _Qmbmph();
    RC rcMbmp = qmbmph->rc;
    bool fMask = qmbmph->fMask;

    // 3DMMv1.0: Intersect prcClip with the boundary of prgbPixels.
    if (pvNil != prcClip && !rcClip.FIntersect(prcClip))
        return;

    // 3DMMv1.0: Translate the mbmp's rc into coordinate system of prgbPixel's rc
    rcMbmp.Offset(xpRef, ypRef);

    // 3DMMv1.0: Intersect rcClip with mbmp's translated rc.
    if (!rcClip.FIntersect(&rcMbmp))
        return;

    // 3DMMv1.0: Set up the region scanner
    if (pvNil != pregnClip)
        regsc.Init(pregnClip, &rcClip);
    else
        regsc.InitRc(&rcClip, &rcClip);

    qcb = _Qrgcb();
    qbRowSrc = (uint8_t *)PvAddBv(qcb, _cbRgcb);
    prgbPixels += LwMul(rcClip.ypTop, cbRow) + rcClip.xpLeft;
    rcMbmp.Offset(-rcClip.xpLeft, -rcClip.ypTop);
    rcClip.OffsetToOrigin();

    // 3DMMv1.0: Step down through rgcb until to top of clipping rc
    for (yp = rcMbmp.ypTop; yp < 0; yp++)
        qbRowSrc += *qcb++;

    // 3DMMv1.0: copy each row appropriately
    for (;;)
    {
        if (regsc.XpCur() == klwMax)
        {
            // 3DMMv1.0: empty strip of the region
            dypT = regsc.DypCur();
            if ((yp += dypT) >= rcClip.ypBottom)
                break;
            regsc.ScanNext(dypT);
            prgbPixels += LwMul(dypT, cbRow);
            for (; dypT > 0; dypT--)
                qbRowSrc += *qcb++;
            continue;
        }
        AssertIn(regsc.XpCur(), 0, rcClip.xpRight);

        qbLastSrc = (qbSrc = qbRowSrc) + *qcb++ - 1;
        pbDst = prgbPixels + rcMbmp.xpLeft;
        pbOn = prgbPixels + regsc.XpCur();
        pbOff = prgbPixels + LwMin(rcClip.xpRight, regsc.XpFetch());

        dxp = 0;
        for (;;)
        {
            while (dxp == 0)
            {
                if (qbSrc >= qbLastSrc)
                    goto LNextRow;
                pbDst += *qbSrc++;
                dxp = *qbSrc++;
            }
            if (pbDst + dxp <= pbOn)
            {
                pbDst += dxp;
                if (!fMask)
                    qbSrc += dxp;
                dxp = 0;
                continue;
            }
            Assert(pbDst + dxp > pbOn, 0);
            if (pbDst >= pbOff)
            {
                // 3DMMv1.0: destination is after pbOff - need to advance the region scan
                if (regsc.XpFetch() == klwMax)
                    break;
                pbOn = prgbPixels + regsc.XpCur();
                pbOff = prgbPixels + LwMin(rcClip.xpRight, regsc.XpFetch());
                continue;
            }

            AssertIn(0, pbOn - pbDst - dxp + 1, pbOff - pbDst);
            if (pbOn > pbDst)
            {
                // 3DMMv1.0: destination is before pbOn
                dxp -= pbOn - pbDst;
                if (!fMask)
                    qbSrc += pbOn - pbDst;
                pbDst = pbOn;
            }

            AssertIn(0, pbOn - pbDst, pbOff - pbDst);
            dxp -= (dxpT = LwMin(dxp, pbOff - pbDst));
            if (fMask)
            {
                FillPb(pbDst, dxpT, qmbmph->bFill);
                pbDst += dxpT;
            }
            else
            {
                while (dxpT--)
                    *pbDst++ = *qbSrc++;
            }
        }

    LNextRow:
        if (++yp >= rcClip.ypBottom)
            break;
        regsc.ScanNext(1);

        // 3DMMv1.0: advance row pointers
        prgbPixels += cbRow;
        qbRowSrc = qbLastSrc + 1;
    }

#endif //! 3DMMv1.0: IN_80386
}

/** 3DMMv1.0: *************************************************************************
    This routine is called to draw the masked bitmap as a mask onto
    prgbPixels.  prgbPixels is assumed to be 1 bit deep.  Zeros are
    written where the MBMP is transparent and ones are written where
    it is non-transparent.
***************************************************************************/
void MBMP::DrawMask(uint8_t *prgbPixels, int32_t cbRow, int32_t dyp, int32_t xpRef, int32_t ypRef, RC *prcClip)
{
    AssertThis(0);
    AssertIn(cbRow, 1, kcbMax);
    AssertIn(dyp, 1, kcbMax);
    AssertPvCb(prgbPixels, LwMul(cbRow, dyp));
    AssertNilOrVarMem(prcClip);

    int32_t yp, xp, dxp;
    uint8_t *qbRowSrc, *qbSrc, *qbLimSrc;
    int16_t *qcb;
    bool fTrans;
    int32_t ib, ibNext;
    uint8_t bMask, bMaskNext;
    MBMPH *qmbmph = _Qmbmph();
    RC rcClip(0, 0, LwMul(cbRow, 8), dyp);
    RC rcMbmp = qmbmph->rc;
    bool fMask = qmbmph->fMask;

    // 3DMMv1.0: Intersect prcClip with the boundary of prgbPixels.
    if (pvNil != prcClip && !rcClip.FIntersect(prcClip))
        return;

    // 3DMMv1.0: Translate the mbmp's rc into coordinate system of prgbPixel's rc
    rcMbmp.Offset(xpRef, ypRef);

    // 3DMMv1.0: Intersect rcClip with mbmp's translated rc.
    if (!rcClip.FIntersect(&rcMbmp))
        return;

    qcb = _Qrgcb();
    qbRowSrc = (uint8_t *)PvAddBv(qcb, _cbRgcb);
    prgbPixels += LwMul(rcClip.ypTop, cbRow);

    // 3DMMv1.0: Step down through rgcb until to top of clipping rc
    for (yp = rcMbmp.ypTop; yp < rcClip.ypTop; yp++)
        qbRowSrc += *qcb++;

    // 3DMMv1.0: copy each row appropriately
    for (; yp < rcClip.ypBottom; yp++)
    {
        qbLimSrc = qbRowSrc + *qcb++;
        qbSrc = qbRowSrc;
        xp = rcMbmp.xpLeft;
        fTrans = fTrue;

        // 3DMMv1.0: Step through row until at left edge of clipping rc
        for (;;)
        {
            if (qbSrc >= qbLimSrc)
            {
                xp = rcClip.xpLeft;
                dxp = rcClip.Dxp();
                fTrans = fTrue;
                break;
            }
            dxp = *qbSrc++;
            if (!fTrans && !fMask)
                qbSrc += dxp;
            if (xp + dxp > rcClip.xpLeft)
            {
                dxp -= rcClip.xpLeft - xp;
                xp = rcClip.xpLeft;
                break;
            }
            xp += dxp;
            fTrans = !fTrans;
        }

        while (xp < rcClip.xpRight)
        {
            if (xp + dxp > rcClip.xpRight)
                dxp = rcClip.xpRight - xp;

            if (dxp > 0)
            {
                // 3DMMv1.0: set or clear dxp bits
                ib = xp >> 3;
                bMask = 0xFF >> (xp & 0x07);
                xp += dxp;
                ibNext = xp >> 3;
                bMaskNext = 0xFF >> (xp & 0x07);
                if (ib == ibNext)
                {
                    if (fTrans)
                        prgbPixels[ib] &= ~bMask | bMaskNext;
                    else
                        prgbPixels[ib] |= bMask & ~bMaskNext;
                }
                else
                {
                    if (fTrans)
                        prgbPixels[ib] &= ~bMask;
                    else
                        prgbPixels[ib] |= bMask;
                    if (ib + 1 < ibNext)
                        FillPb(prgbPixels + ib + 1, ibNext - ib - 1, fTrans ? 0 : 0xFF);
                    if (fTrans)
                        prgbPixels[ibNext] &= bMaskNext;
                    else
                        prgbPixels[ibNext] |= ~bMaskNext;
                }
            }
            if (xp >= rcClip.xpRight)
                break;

            if (qbSrc >= qbLimSrc)
            {
                dxp = rcClip.xpRight - xp;
                fTrans = fTrue;
            }
            else
            {
                fTrans = !fTrans;
                dxp = *qbSrc++;
                if (!fTrans && !fMask)
                    qbSrc += dxp;
            }
        }

        // 3DMMv1.0: advance row pointers
        prgbPixels += cbRow;
        qbRowSrc = qbLimSrc;
    }
}
