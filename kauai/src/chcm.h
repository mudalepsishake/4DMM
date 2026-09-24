/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Header file for the CHCM class - the chunky compiler class, and
    CHLX - its lexer.

***************************************************************************/
#ifndef CHCM_H
#define CHCM_H

// 3DMMv1.0: token types
enum
{
    ttChunk = ttLimBase, // 3DMMv1.0: chunk definition
    ttChild,             // 3DMMv1.0: child descriptor
    ttParent,            // 3DMMv1.0: parent descriptor
    ttAlign,             // 3DMMv1.0: align command
    ttFile,              // 3DMMv1.0: file import command
    ttMeta,              // 3DMMv1.0: metafile import command
    ttBitmap,            // 3DMMv1.0: bitmap import command
    ttFree,              // 3DMMv1.0: for AL, AG, AST - item is free
    ttItem,              // 3DMMv1.0: for GL, etc - start of data for new item
    ttVar,               // 3DMMv1.0: for GG and AG - variable sized data
    ttGl,                // 3DMMv1.0: GL command
    ttAl,                // 3DMMv1.0: AL command
    ttGg,                // 3DMMv1.0: GG command
    ttAg,                // 3DMMv1.0: AG command
    ttGst,               // 3DMMv1.0: GST command
    ttAst,               // 3DMMv1.0: AST command
    ttScript,            // 3DMMv1.0: infix script
    ttScriptP,           // 3DMMv1.0: postfix script
    ttModeStn,           // 3DMMv1.0: change mode to store strings as stn's
    ttModeStz,           // 3DMMv1.0: change mode to store strings as stz's
    ttModeSz,            // 3DMMv1.0: change mode to store strings as sz's
    ttModeSt,            // 3DMMv1.0: change mode to store strings as st's
    ttModeByte,          // 3DMMv1.0: change mode to accept a byte
    ttModeShort,         // 3DMMv1.0: change mode to accept a short
    ttModeLong,          // 3DMMv1.0: change mode to accept a long
    ttEndChunk,          // 3DMMv1.0: end of chunk
    ttAdopt,             // 3DMMv1.0: adopt command
    ttMacBo,             // 3DMMv1.0: use Mac byte order and OSK
    ttWinBo,             // 3DMMv1.0: use Win byte order and OSK
    ttMacOsk,            // 3DMMv1.0: use Mac byte order and OSK
    ttWinOsk,            // 3DMMv1.0: use Win byte order and OSK
    ttBo,                // 3DMMv1.0: insert the current byte order
    ttOsk,               // 3DMMv1.0: insert the current OSK
    ttMask,              // 3DMMv1.0: MASK command
    ttLoner,             // 3DMMv1.0: LONER command
    ttCursor,            // 3DMMv1.0: CURSOR command
    ttPalette,           // 3DMMv1.0: PALETTE command
    ttPrePacked,         // 3DMMv1.0: mark the change as packed
    ttPack,              // 3DMMv1.0: pack the data
    ttPackedFile,        // 3DMMv1.0: packed file import command
    ttMidi,              // 3DMMv1.0: midi file import command
    ttPackFmt,           // 3DMMv1.0: format to pack in
    ttSubFile,           // 3DMMv1.0: start an embedded chunk forest

    ttLimChlx
};

// 3DMMv1.0: lookup table for keywords
struct KEYTT
{
    const PCSZ pszKeyword;
    int32_t tt;
};

/** 3DMMv1.0: *************************************************************************
    Chunky Compiler lexer class.
***************************************************************************/
typedef class CHLX *PCHLX;
#define CHLX_PAR LEXB
#define kclsCHLX KLCONST4('C', 'H', 'L', 'X')
class CHLX : public CHLX_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(CHLX)

  protected:
    PGST _pgstVariables;
    PCSZ _pszSearchPath;

    bool _FDoSet(PTOK ptok);

  public:
    CHLX(PBSF pbsf, PSTN pstnFile, PCSZ pszSearchPath);
    ~CHLX(void);

    // 3DMMv1.0: override the LEXB FGetTok to resolve variables, hande SET
    // 3DMMv1.0: and recognize our additional key words
    virtual bool FGetTok(PTOK ptok) override;
    virtual bool FGetTokSkipSemi(PTOK ptok); // 3DMMv1.0: also skip ';' & ','
    virtual bool FGetPath(FNI *pfni);        // 3DMMv1.0: read a path
};

// 3DMMv1.0: error types
enum
{
    ertNil = 0,
    ertOom,
    ertOpenFile,
    ertReadMeta,
    ertRangeByte,
    ertRangeShort,
    ertBufData,
    ertParenOpen,
    ertEof,
    ertNeedString,
    ertNeedNumber,
    ertBadToken,
    ertParenClose,
    ertChunkHead,
    ertDupChunk,
    ertBodyChildHead,
    ertChildMissing,
    ertCycle,
    ertBodyParentHead,
    ertParentMissing,
    ertBodyAlignRange,
    ertBodyFile,
    ertNeedEndChunk,
    ertListHead,
    ertListEntrySize,
    ertVarUndefined,
    ertItemOverflow,
    ertBadFree,
    ertSyntax,
    ertGroupHead,
    ertGroupEntrySize,
    ertGstHead,
    ertGstEntrySize,
    ertScript,
    ertAdoptHead,
    ertNeedChunk,
    ertBodyBitmapHead,
    ertReadBitmap,
    ertBadScript,
    ertReadCursor,
    ertPackedFile,
    ertReadMidi,
    ertBadPackFmt,
    ertLonerInSub,
    ertNoEndSubFile,
    ertMetafileNotSupported,
    ertLim
};

// 3DMMv1.0: string modes
enum
{
    smStn,
    smStz,
    smSz,
    smSt
};

#define kcbMinAlign 2
#define kcbMaxAlign 1024

/** 3DMMv1.0: *************************************************************************
    Base chunky compiler class
***************************************************************************/
typedef class CHCM *PCHCM;
#define CHCM_PAR BASE
#define kclsCHCM KLCONST4('C', 'H', 'C', 'M')
class CHCM : public CHCM_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(CHCM)

  protected:
    // 3DMMv1.0: Chunk sub file context
    struct CSFC
    {
        PCFL pcfl;
        CTG ctg;
        CNO cno;
        bool fPack;
    };

    PGL _pglcsfc; // 3DMMv1.0: the stack of CSFCs for sub files

    PCFL _pcfl;       // 3DMMv1.0: current sub file
    PGL _pglckiLoner; // 3DMMv1.0: the chunks that must be loners

    BSF _bsf;       // 3DMMv1.0: temporary buffer for the chunk data
    PCHLX _pchlx;   // 3DMMv1.0: lexer for compiling
    int32_t _sm;    // 3DMMv1.0: current string mode
    int32_t _cbNum; // 3DMMv1.0: current numerical size (1, 2, or 4)
    int16_t _bo;    // 3DMMv1.0: current byte order and osk
    int16_t _osk;
    PMSNK _pmsnkError;  // 3DMMv1.0: error message sink
    int32_t _cactError; // 3DMMv1.0: how many errors we've encountered
    PSZ _pszSearchPath; // 3DMMEx: Search path for locating subfiles

  protected:
    struct PHP // 3DMMv1.0: parenthesized header parameter
    {
        int32_t lw;
        PSTN pstn;
    };

    void _Error(int32_t ert, PCSZ pszMessage = pvNil);
    void _GetRgbFromLw(int32_t lw, uint8_t *prgb);
    void _ErrorOnData(PCSZ pszPreceed);
    bool _FParseParenHeader(PHP *prgphp, int32_t cphpMax, int32_t *pcphp);
    bool _FGetCleanTok(TOK *ptok, bool fEofOk = fFalse);
    void _SkipPastTok(int32_t tt);
    void _ParseChunkHeader(CTG *pctg, CNO *pcno);
    void _AppendString(PSTN pstnValue);
    void _AppendNumber(int32_t lwValue);
    void _ParseBodyChild(CTG ctg, CNO cno);
    void _ParseBodyParent(CTG ctg, CNO cno);
    void _ParseBodyAlign(void);
    void _ParseBodyFile(void);

    void _StartSubFile(bool fPack, CTG ctg, CNO cno);
    void _EndSubFile(void);

    void _ParseBodyMeta(bool fPack, CTG ctg, CNO cno);
    void _ParseBodyBitmap(bool fPack, bool fMask, CTG ctg, CNO cno);
    void _ParseBodyPalette(bool fPack, CTG ctg, CNO cno);
    void _ParseBodyMidi(bool fPack, CTG ctg, CNO cno);
    void _ParseBodyCursor(bool fPack, CTG ctg, CNO cno);
    bool _FParseData(PTOK ptok);
    void _ParseBodyList(bool fPack, bool fAl, CTG ctg, CNO cno);
    void _ParseBodyGroup(bool fPack, bool fAg, CTG ctg, CNO cno);
    void _ParseBodyStringTable(bool fPack, bool fAst, CTG ctg, CNO cno);
    void _ParseBodyScript(bool fPack, bool fInfix, CTG ctg, CNO cno);
    void _ParseBodyPackedFile(bool *pfPacked);
    void _ParseChunkBody(CTG ctg, CNO cno);
    void _ParseAdopt(void);
    void _ParsePackFmt(void);

    bool _FPrepWrite(bool fPack, int32_t cb, CTG ctg, CNO cno, PBLCK pblck);
    bool _FEndWrite(bool fPack, CTG ctg, CNO cno, PBLCK pblck);

  public:
    CHCM(void);
    ~CHCM(void);

    bool FError(void)
    {
        return _cactError > 0;
    }

    PCFL PcflCompile(PFNI pfniSrc, PFNI pfniDst, PMSNK pmsnk);
    PCFL PcflCompile(PBSF pbsfSrc, PSTN pstnFile, PFNI pfniDst, PMSNK pmsnk);

    bool FSetSearchPath(PCSZ pszSearchPath);
};

/** 3DMMv1.0: *************************************************************************
    Chunky decompiler class.
***************************************************************************/
typedef class CHDC *PCHDC;
#define CHDC_PAR BASE
#define kclsCHDC KLCONST4('C', 'H', 'D', 'C')
class CHDC : public CHDC_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(CHDC)

  protected:
    int32_t _ert; // 3DMMv1.0: error type
    PCFL _pcfl;   // 3DMMv1.0: the chunky file to read from
    BSF _bsf;     // 3DMMv1.0: temporary buffer for the chunk data
    int16_t _bo;  // 3DMMv1.0: current byte order and osk
    int16_t _osk;
    CHSE _chse; // 3DMMv1.0: chunky source emitter

  protected:
    bool _FDumpScript(CKI *pcki);
    bool _FDumpList(PBLCK pblck, bool fAl);
    bool _FDumpGroup(PBLCK pblck, bool fAg);
    bool _FDumpStringTable(PBLCK pblck, bool fAst);
    void _WritePack(int32_t cfmt);

  public:
    CHDC(void);
    ~CHDC(void);

    bool FError(void)
    {
        return ertNil != _ert;
    }

    bool FDecompile(PCFL pcflSrc, PMSNK pmsnk, PMSNK pmsnkError);
};

#endif // 3DMMv1.0: CHCM_H
