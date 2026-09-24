/** 3DMMv1.0: ***********************************************************************

    Texture map (TMAP)

    This manages I/O and caching for BPMPs

*************************************************************************/
#ifndef TMAP_H
#define TMAP_H

const CTG kctgTmap = KLCONST4('T', 'M', 'A', 'P');
const CTG kctgTxxf = KLCONST4('T', 'X', 'X', 'F');

// 3DMMv1.0: tmap on file
struct TMAPF
{
    int16_t bo;
    int16_t osk;
    int16_t cbRow;
    uint8_t type;
    uint8_t grftmap;
    int16_t xpLeft;
    int16_t ypTop;
    int16_t dxp;
    int16_t dyp;
    int16_t xpOrigin;
    int16_t ypOrigin;
    // 3DMMv1.0: void *rgb; 		// pixels follow immediately after TMAPF
};
VERIFY_STRUCT_SIZE(TMAPF, 20);
const uint32_t kbomTmapf = 0x54555000;

/* 3DMMv1.0: A TeXture XransForm on File */
typedef struct _txxff
{
    int16_t bo;  // 3DMMv1.0: byte order
    int16_t osk; // 3DMMv1.0: OS kind
    BMAT23 bmat23;
} TXXFF, *PTXXFF;
VERIFY_STRUCT_SIZE(TXXFF, 28);
const BOM kbomTxxff = 0x5FFF0000;

// 3DMMv1.0: REVIEW *****: should TMAPs have shade table chunks under them, or
// 3DMMv1.0:   is the shade table a global animal?  Right now it's global.

/** 3DMMv1.0: **************************************
    The TMAP class
****************************************/
typedef class TMAP *PTMAP;
#define TMAP_PAR BACO
#define kclsTMAP KLCONST4('T', 'M', 'A', 'P')
class TMAP : public TMAP_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
  protected:
    BPMP _bpmp;
    bool _fImported; // 3DMMv1.0: if fTrue, BRender allocated the pixels
                     // 3DMMv1.0: if fFalse, we allocated the pixels
  protected:
    TMAP(void)
    {
    } // 3DMMv1.0: can't instantiate directly; must use PtmapRead
    bool _FConvertToRgb888(void);
#ifdef NOT_YET_REVIEWED
    void TMAP::_SortInverseTable(uint8_t *prgb, int32_t cbRgb, BRCLR brclrLo, BRCLR brclrHi);
#endif // 3DMMv1.0: NOT_YET_REVIEWED
  public:
    ~TMAP(void);

    // 3DMMv1.0:  REVIEW *****(peted): MBMP's ...Read function just takes a PBLCK; this
    // 3DMMv1.0:  is more like the FRead... function, just without the BACO stuff.  Why
    // 3DMMv1.0:  the difference?
    // 3DMMv1.0:	Addendum: to enable compiling 'TMAP' chunks, I added an FWrite that does
    // 3DMMv1.0:	take just a PBLCK.  Should this be necessary for PtmapRead in the future,
    // 3DMMv1.0:	it's a simple matter of extracting the code in PtmapRead that is needed,
    // 3DMMv1.0:	like I did for FWrite.
    static PTMAP PtmapRead(PCFL pcfl, CTG ctg, CNO cno, bool fKeepIndexed = fFalse);
    bool FWrite(PCFL pcfl, CTG ctg, CNO *pcno);

    // 3DMMv1.0:	a chunky resource reader for a TMAP
    static bool FReadTmap(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);

    // 3DMMv1.0:	Given a BPMP (a Brender br_pixelmap), create a TMAP
    static PTMAP PtmapNewFromBpmp(BPMP *pbpmp);

    // 3DMMv1.0:	Give back the bpmp for this TMAP
    BPMP *Pbpmp(void)
    {
        return &_bpmp;
    }

    // 3DMMv1.0:	Reads a .bmp file.
    static PTMAP PtmapReadNative(FNI *pfni, PGL pglclr = pvNil);

    // 3DMMv1.0: Writes a standalone TMAP-chunk file (not a .chk)
    bool FWriteTmapChkFile(PFNI pfniDst, bool fCompress, PMSNK pmsnkErr = pvNil);

    // 3DMMv1.0: Creates a TMAP from the width, height, and an array of bytes
    static PTMAP PtmapNew(uint8_t *prgbPixels, int32_t dxWidth, int32_t dxHeight);

    // 3DMMv1.0: Some useful file methods
    int32_t CbOnFile(void) override
    {
        return (SIZEOF(TMAPF) + LwMul(_bpmp.row_bytes, _bpmp.height));
    }
    bool FWrite(PBLCK pblck) override;

#ifdef NOT_YET_REVIEWED
    // 3DMMv1.0: Useful shade-table type method
    uint8_t *PrgbBuildInverseTable(void);
#endif // 3DMMv1.0: NOT_YET_REVIEWED
};

#endif // 3DMMv1.0: TMAP_H
