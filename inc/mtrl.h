/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: ***********************************************************************

    mtrl.h: Material and custom material classes

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BACO ---> MTRL
    BASE ---> BACO ---> CMTL

*************************************************************************/
#ifndef MTRL_H
#define MTRL_H

// 3DMMv1.0: CMTL on File
struct CMTLF
{
    int16_t bo;
    int16_t osk;
    int32_t ibset; // 3DMMv1.0: which body part set this CMTL attaches to
};
VERIFY_STRUCT_SIZE(CMTLF, 8);
const BOM kbomCmtlf = 0x5c000000;

// 3DMMv1.0: material on file (MTRL chunk)
struct MTRLF
{
    int16_t bo;          // 3DMMv1.0: byte order
    int16_t osk;         // 3DMMv1.0: OS kind
    br_colour brc;       // 3DMMv1.0: RGB color
    br_ufraction brufKa; // 3DMMv1.0: ambient component
    br_ufraction brufKd; // 3DMMv1.0: diffuse component
    br_ufraction brufKs; // 3DMMv1.0: specular component
    uint8_t bIndexBase;  // 3DMMv1.0: base of palette for this color
    uint8_t cIndexRange; // 3DMMv1.0: count of entries in palette for this color
    BRS rPower;          // 3DMMv1.0: specular exponent
};
VERIFY_STRUCT_SIZE(MTRLF, 20);
const BOM kbomMtrlf = 0x5D530000;

/** 3DMMv1.0: **************************************
    The MTRL class.  There are two kinds
    of MTRLs: solid-color MTRLs and
    texmap materials.  Texmap MTRLs have
    TMAPs under the MTRL chunk with chid
    0.
****************************************/
typedef class MTRL *PMTRL;
#define MTRL_PAR BACO
#define kclsMTRL KLCONST4('M', 'T', 'R', 'L')
class MTRL : public MTRL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    static PTMAP _ptmapShadeTable; // 3DMMv1.0: shade table for all MTRLs
    PBMTL _pbmtl;

  protected:
    MTRL(void)
    {
        _pbmtl = pvNil;
    } // 3DMMv1.0: can't instantiate directly; must use FReadMtrl
    bool _FInit(PCRF pcrf, CTG ctg, CNO cno);

  public:
    static bool FSetShadeTable(PCFL pcfl, CTG ctg, CNO cno);
    static PMTRL PmtrlNew(int32_t iclrBase = ivNil, int32_t cclr = ivNil);
    static bool FReadMtrl(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);
    static PMTRL PmtrlNewFromPix(PFNI pfni);
    static PMTRL PmtrlNewFromBmp(PFNI pfni, PGL pglclr = pvNil);
    static PMTRL PmtrlFromBmtl(PBMTL pbmtl);
    ~MTRL(void);
    PTMAP Ptmap(void);
    PBMTL Pbmtl(void)
    {
        return _pbmtl;
    }
    bool FWrite(PCFL pcfl, CTG ctg, CNO *pcno);
#ifdef DEBUG
    static void MarkShadeTable(void);
#endif // 3DMMv1.0: DEBUG
};

/** 3DMMv1.0: **************************************
    The CMTL (custom material) class
    This manages a set of materials to
    apply to a body part set
****************************************/
typedef class CMTL *PCMTL;
#define CMTL_PAR BACO
#define kclsCMTL KLCONST4('C', 'M', 'T', 'L')
class CMTL : public CMTL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PMTRL *_prgpmtrl; // 3DMMv1.0: _cbprt PMTRLs, one per body part in this CMTL's set
    PMODL *_prgpmodl; // 3DMMv1.0: _cbprt PMODLs, one per body part in this CMTL's set
    int32_t _cbprt;   // 3DMMv1.0: count of body parts in this CMTL
    int32_t _ibset;   // 3DMMv1.0: body part set that this CMTL should be applied to

  protected:
    bool _FInit(PCRF pcrf, CTG ctg, CNO cno);
    CMTL(void)
    {
    } // 3DMMv1.0: can't instantiate directly; must use PcmtlRead

  public:
    static PCMTL PcmtlNew(int32_t ibset, int32_t cbprt, PMTRL *prgpmtrl);
    static bool FReadCmtl(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);
    static bool FHasModels(PCFL pcfl, CTG ctg, CNO cno);
    static bool FEqualModels(PCFL pcfl, CNO cno1, CNO cno2);
    ~CMTL(void);
    PBMTL Pbmtl(int32_t ibmtl);
    PMODL Pmodl(int32_t imodl);
    int32_t Ibset(void)
    {
        return _ibset;
    }
    int32_t Cbprt(void)
    {
        return _cbprt;
    }
    bool FHasModels(void);
};

#endif // 3DMMv1.0: !MTRL_H
