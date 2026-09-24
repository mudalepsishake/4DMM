/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    A lexer for command scripts.

***************************************************************************/
#ifndef LEX_H
#define LEX_H

enum
{
    fctNil = 0,   // 3DMMv1.0: invalid character
    fctLow = 1,   // 3DMMv1.0: lowercase letter
    fctUpp = 2,   // 3DMMv1.0: uppercase letter
    fctOct = 4,   // 3DMMv1.0: octal
    fctDec = 8,   // 3DMMv1.0: digit
    fctHex = 16,  // 3DMMv1.0: hex digit
    fctSpc = 32,  // 3DMMv1.0: space character
    fctOp1 = 64,  // 3DMMv1.0: first character of a multi-character operator
    fctOp2 = 128, // 3DMMv1.0: last character of a multi-character operator
    fctOpr = 256, // 3DMMv1.0: lone character operator
    fctQuo = 512, // 3DMMv1.0: quote character
};
#define kgrfctDigit (fctOct | fctDec | fctHex)

enum
{
    ttNil,
    ttError,  // 3DMMv1.0: bad token
    ttLong,   // 3DMMv1.0: numeric constant
    ttName,   // 3DMMv1.0: identifier
    ttString, // 3DMMv1.0: string constant

    ttAdd,        // 3DMMv1.0: +
    ttSub,        // 3DMMv1.0: -
    ttMul,        // 3DMMv1.0: *
    ttDiv,        // 3DMMv1.0: /
    ttMod,        // 3DMMv1.0: %
    ttInc,        // 3DMMv1.0: ++
    ttDec,        // 3DMMv1.0: --
    ttBOr,        // 3DMMv1.0: |
    ttBAnd,       // 3DMMv1.0: &
    ttBXor,       // 3DMMv1.0: ^
    ttBNot,       // 3DMMv1.0: ~
    ttShr,        // 3DMMv1.0: >>
    ttShl,        // 3DMMv1.0: <<
    ttLOr,        // 3DMMv1.0: ||
    ttLAnd,       // 3DMMv1.0: &&
    ttLXor,       // 3DMMv1.0: ^^
    ttEq,         // 3DMMv1.0: ==
    ttNe,         // 3DMMv1.0: !=
    ttGt,         // 3DMMv1.0: >
    ttGe,         // 3DMMv1.0: >=
    ttLt,         // 3DMMv1.0: <
    ttLe,         // 3DMMv1.0: <=
    ttLNot,       // 3DMMv1.0: !
    ttAssign,     // 3DMMv1.0: =
    ttAAdd,       // 3DMMv1.0: +=
    ttASub,       // 3DMMv1.0: -=
    ttAMul,       // 3DMMv1.0: *=
    ttADiv,       // 3DMMv1.0: /=
    ttAMod,       // 3DMMv1.0: %=
    ttABOr,       // 3DMMv1.0: |=
    ttABAnd,      // 3DMMv1.0: &=
    ttABXor,      // 3DMMv1.0: ^=
    ttAShr,       // 3DMMv1.0: >>=
    ttAShl,       // 3DMMv1.0: <<=
    ttArrow,      // 3DMMv1.0: ->
    ttDot,        // 3DMMv1.0: .
    ttQuery,      // 3DMMv1.0: ?
    ttColon,      // 3DMMv1.0: :
    ttComma,      // 3DMMv1.0: ,
    ttSemi,       // 3DMMv1.0: ;
    ttOpenRef,    // 3DMMv1.0: [
    ttCloseRef,   // 3DMMv1.0: ]
    ttOpenParen,  // 3DMMv1.0: (
    ttCloseParen, // 3DMMv1.0: )
    ttOpenBrace,  // 3DMMv1.0: {
    ttCloseBrace, // 3DMMv1.0: }
    ttPound,      // 3DMMv1.0: #
    ttDollar,     // 3DMMv1.0: $
    ttAt,         // 3DMMv1.0: @
    ttAccent,     // 3DMMv1.0: `
    ttBackSlash,  // 3DMMv1.0: backslash character (\)
    ttScope,      // 3DMMv1.0: ::

    ttLimBase
};

struct TOK
{
    int32_t tt;
    int32_t lw;
    STN stn;
};
typedef TOK *PTOK;

/** 3DMMv1.0: *************************************************************************
    Base lexer.
***************************************************************************/
#define kcchLexbBuf 512

typedef class LEXB *PLEXB;
#define LEXB_PAR BASE
#define kclsLEXB KLCONST4('L', 'E', 'X', 'B')
class LEXB : public LEXB_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    static uint16_t _mpchgrfct[];

    PFIL _pfil; // 3DMMv1.0: exactly one of _pfil, _pbsf should be non-nil
    PBSF _pbsf;
    STN _stnFile;
    int32_t _lwLine;  // 3DMMv1.0: which line
    int32_t _ichLine; // 3DMMv1.0: which character on the line

    FP _fpCur;
    FP _fpMac;
    int32_t _ichLim;
    int32_t _ichCur;
    achar _rgch[kcchLexbBuf];
    bool _fLineStart : 1;
    bool _fSkipToNextLine : 1;
    bool _fUnionStrings : 1;

    uint32_t _GrfctCh(achar ch)
    {
        return (uchar)ch < 128 ? _mpchgrfct[(uint8_t)ch] : fctNil;
    }
    bool _FFetchRgch(achar *prgch, int32_t cch = 1);
    void _Advance(int32_t cch = 1)
    {
        _ichCur += cch;
        _ichLine += cch;
    }
    bool _FSkipWhiteSpace(void);
    virtual void _ReadNumber(int32_t *plw, achar ch, int32_t lwBase, int32_t cchMax);
    virtual void _ReadNumTok(PTOK ptok, achar ch, int32_t lwBase, int32_t cchMax)
    {
        _ReadNumber(&ptok->lw, ch, lwBase, cchMax);
    }
    bool _FReadHex(int32_t *plw);
    bool _FReadControlCh(achar *pch);

  public:
    LEXB(PFIL pfil, bool fUnionStrings = fTrue);
    LEXB(PBSF pbsf, PSTN pstnFile, bool fUnionStrings = fTrue);
    ~LEXB(void);

    virtual bool FGetTok(PTOK ptok);
    virtual int32_t CbExtra(void);
    virtual void GetExtra(void *pv);

    void GetStnFile(PSTN pstn);
    int32_t LwLine(void)
    {
        return _lwLine;
    }
    int32_t IchLine(void)
    {
        return _ichLine;
    }
};

#endif //! 3DMMv1.0: LEX_H
