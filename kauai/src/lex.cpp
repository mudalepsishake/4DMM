/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Basic lexer class

***************************************************************************/
#include <stdio.h>
#include "util.h"
ASSERTNAME

RTCLASS(LEXB)

// 3DMMv1.0: #line handling
achar _szMsvcPoundLine[] = PszLit("#line");
#define kcchMsvcPoundLine (CvFromRgv(_szMsvcPoundLine) - 1)

// 3DMMEx: #<space> handling
achar _szGccPoundLine[] = PszLit("# ");
#define kcchGccPoundLine (CvFromRgv(_szGccPoundLine) - 1)

uint16_t LEXB::_mpchgrfct[128] = {
    // 3DMMv1.0: 0x00 - 0x07
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    // 3DMMv1.0: 0x08; 0x09=tab; 0x0A=line-feed; 0x0B; 0x0C; 0x0D=return; 0x0E; 0x0F
    fctNil,
    fctSpc,
    fctSpc,
    fctNil,
    fctNil,
    fctSpc,
    fctNil,
    fctNil,

    // 3DMMv1.0: 0x10 - 0x17
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    // 3DMMv1.0: 0x18; 0x19; 0x1A=Ctrl-Z; 0x1B - 0x1F
    fctNil,
    fctNil,
    fctSpc,
    fctNil,
    fctNil,
    fctNil,
    fctNil,
    fctNil,

    // 3DMMv1.0: space ! " #
    fctSpc,
    fctOpr | fctOp1,
    fctQuo,
    fctOpr,
    // 3DMMv1.0: $ % & '
    fctOpr,
    fctOpr | fctOp1,
    fctOpr | fctOp1 | fctOp2,
    fctQuo,
    // 3DMMv1.0: ( ) * +
    fctOpr,
    fctOpr,
    fctOpr | fctOp1,
    fctOpr | fctOp1 | fctOp2,
    // 3DMMv1.0: , - . /
    fctOpr,
    fctOpr | fctOp1 | fctOp2,
    fctOpr,
    fctOpr | fctOp1,

    // 3DMMv1.0: 0 1 2 3
    kgrfctDigit,
    kgrfctDigit,
    kgrfctDigit,
    kgrfctDigit,
    // 3DMMv1.0: 4 5 6 7
    kgrfctDigit,
    kgrfctDigit,
    kgrfctDigit,
    kgrfctDigit,
    // 3DMMv1.0: 8 9 : ;
    fctDec | fctHex,
    fctDec | fctHex,
    fctOpr | fctOp1 | fctOp2,
    fctOpr,
    // 3DMMv1.0: < = > ?
    fctOpr | fctOp1 | fctOp2,
    fctOpr | fctOp1 | fctOp2,
    fctOpr | fctOp1 | fctOp2,
    fctOpr,

    // 3DMMv1.0: @ A B C
    fctOpr,
    fctUpp | fctHex,
    fctUpp | fctHex,
    fctUpp | fctHex,
    // 3DMMv1.0: D E F G
    fctUpp | fctHex,
    fctUpp | fctHex,
    fctUpp | fctHex,
    fctUpp,
    // 3DMMv1.0: H I J K L M N O
    fctUpp,
    fctUpp,
    fctUpp,
    fctUpp,
    fctUpp,
    fctUpp,
    fctUpp,
    fctUpp,

    // 3DMMv1.0: P Q R S T U V W
    fctUpp,
    fctUpp,
    fctUpp,
    fctUpp,
    fctUpp,
    fctUpp,
    fctUpp,
    fctUpp,
    // 3DMMv1.0: X Y Z [ \ ] ^ _
    fctUpp,
    fctUpp,
    fctUpp,
    fctOpr,
    fctOpr,
    fctOpr,
    fctOpr | fctOp1 | fctOp2,
    fctUpp | fctLow,

    // 3DMMv1.0: ` a b c
    fctOpr,
    fctLow | fctHex,
    fctLow | fctHex,
    fctLow | fctHex,
    // 3DMMv1.0: d e f g
    fctLow | fctHex,
    fctLow | fctHex,
    fctLow | fctHex,
    fctLow,
    // 3DMMv1.0: h i j k l m n o
    fctLow,
    fctLow,
    fctLow,
    fctLow,
    fctLow,
    fctLow,
    fctLow,
    fctLow,

    // 3DMMv1.0: p q r s t u v w
    fctLow,
    fctLow,
    fctLow,
    fctLow,
    fctLow,
    fctLow,
    fctLow,
    fctLow,
    // 3DMMv1.0: x y z {
    fctLow,
    fctLow,
    fctLow,
    fctOpr,
    // 3DMMv1.0: | } ~ 0x7F=del
    fctOpr | fctOp1 | fctOp2,
    fctOpr,
    fctOpr | fctOp1,
    fctNil,
};

// 3DMMv1.0: token values for single characters
#define kchMinTok ChLit('!')
int16_t _rgtt[] = {
    // 3DMMv1.0: ! " # $ % & '
    ttLNot, ttNil, ttPound, ttDollar, ttMod, ttBAnd, ttNil,
    // 3DMMv1.0: ( ) * + , - . /
    ttOpenParen, ttCloseParen, ttMul, ttAdd, ttComma, ttSub, ttDot, ttDiv,
    // 3DMMv1.0: 0-7
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: 8 9 : ; < = > ?
    ttNil, ttNil, ttColon, ttSemi, ttLt, ttAssign, ttGt, ttQuery,
    // 3DMMv1.0: @ A-G
    ttAt, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: H-O
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: P-W
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: X Y Z [ \ ] ^ _
    ttNil, ttNil, ttNil, ttOpenRef, ttBackSlash, ttCloseRef, ttBXor, ttNil,
    // 3DMMv1.0: ` a-g
    ttAccent, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: h-o
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: p-w
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: x y a { | } ~
    ttNil, ttNil, ttNil, ttOpenBrace, ttBOr, ttCloseBrace, ttBNot};
int32_t _TtFromCh(achar ch);

/** 3DMMv1.0: *************************************************************************
    Return the token type of a single character operator.
***************************************************************************/
int32_t _TtFromCh(achar ch)
{
    AssertIn(ch, kchMinTok, kchMinTok + SIZEOF(_rgtt) / SIZEOF(_rgtt[0]));
    return _rgtt[(uint8_t)ch - kchMinTok];
}

#define kchMinDouble ChLit('&')
#define kchLastDouble ChLit('|')
int16_t _rgttDouble[] = {
    // 3DMMv1.0: & '
    ttLAnd, ttNil,
    // 3DMMv1.0: ( ) * + , - . /
    ttNil, ttNil, ttNil, ttInc, ttNil, ttDec, ttNil, ttNil,
    // 3DMMv1.0: 0-7
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: 8 9 : ; < = > ?
    ttNil, ttNil, ttScope, ttNil, ttShl, ttEq, ttShr, ttNil,
    // 3DMMv1.0: @ A-G
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: H-O
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: P-W
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: X Y Z [ \ ] ^ _
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttLXor, ttNil,
    // 3DMMv1.0: ` a-g
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: h-o
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: p-w
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: x y a { |
    ttNil, ttNil, ttNil, ttNil, ttLOr};

#define kchMinEqual ChLit('!')
#define kchLastEqual ChLit('|')
int16_t _rgttEqual[] = {
    // 3DMMv1.0: ! " # $ % & '
    ttNe, ttNil, ttNil, ttNil, ttAMod, ttABAnd, ttNil,
    // 3DMMv1.0: ( ) * + , - . /
    ttNil, ttNil, ttAMul, ttAAdd, ttNil, ttASub, ttNil, ttADiv,
    // 3DMMv1.0: 0-7
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: 8 9 : ; < = > ?
    ttNil, ttNil, ttNil, ttNil, ttLe, ttEq, ttGe, ttNil,
    // 3DMMv1.0: @ A-G
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: H-O
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: P-W
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: X Y Z [ \ ] ^ _
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttABXor, ttNil,
    // 3DMMv1.0: ` a-g
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: h-o
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: p-w
    ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil, ttNil,
    // 3DMMv1.0: x y a { |
    ttNil, ttNil, ttNil, ttNil, ttABOr};
int32_t _TtFromChCh(achar ch1, achar ch2);

/** 3DMMv1.0: *************************************************************************
    Return the token type of a double character token.
***************************************************************************/
int32_t _TtFromChCh(achar ch1, achar ch2)
{
    if (ch1 == ch2)
    {
        return FIn(ch1, kchMinDouble, kchLastDouble + 1) ? _rgttDouble[(uint8_t)ch1 - kchMinDouble] : ttNil;
    }

    if (ch2 == ChLit('='))
    {
        return FIn(ch1, kchMinEqual, kchLastEqual + 1) ? _rgttEqual[ch1 - kchMinEqual] : ttNil;
    }

    if (ch1 == ChLit('-') && ch2 == ChLit('>'))
        return ttArrow;

    return ttNil;
}

/** 3DMMv1.0: *************************************************************************
    Constructor for the lexer.
***************************************************************************/
LEXB::LEXB(PFIL pfil, bool fUnionStrings)
{
    AssertPo(pfil, 0);

    _pfil = pfil;
    _pbsf = pvNil;
    _pfil->AddRef();
    _pfil->GetStnPath(&_stnFile);
    _lwLine = 1;
    _ichLine = 0;
    _fpCur = 0;
    _fpMac = pfil->FpMac();
    _ichLim = _ichCur = 0;
    _fLineStart = fTrue;
    _fSkipToNextLine = fFalse;
    _fUnionStrings = fUnionStrings;
    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Constructor for the lexer.
***************************************************************************/
LEXB::LEXB(PBSF pbsf, PSTN pstnFile, bool fUnionStrings)
{
    AssertPo(pbsf, 0);
    AssertPo(pstnFile, 0);

    _pfil = pvNil;
    _pbsf = pbsf;
    _pbsf->AddRef();
    _stnFile = *pstnFile;
    _lwLine = 1;
    _ichLine = 0;
    _fpCur = 0;
    _fpMac = pbsf->IbMac();
    _ichLim = _ichCur = 0;
    _fLineStart = fTrue;
    _fSkipToNextLine = fFalse;
    _fUnionStrings = fUnionStrings;
    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Destructor for the lexer.
***************************************************************************/
LEXB::~LEXB(void)
{
    ReleasePpo(&_pfil);
    ReleasePpo(&_pbsf);
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a LEXB.
***************************************************************************/
void LEXB::AssertValid(uint32_t grf)
{
    LEXB_PAR::AssertValid(0);
    AssertNilOrPo(_pfil, 0);
    AssertNilOrPo(_pbsf, 0);
    Assert((_pfil == pvNil) != (_pbsf == pvNil), "exactly one of _pfil, _pbsf should be non-nil");
    AssertPo(&_stnFile, 0);
    AssertIn(_lwLine, 0, kcbMax);
    AssertIn(_ichLine, 0, kcbMax);
    AssertIn(_fpCur, 0, _fpMac + 1);
    AssertIn(_fpMac, 0, kcbMax);
    AssertIn(_ichCur, 0, _ichLim + 1);
    AssertIn(_ichLim, 0, CvFromRgv(_rgch) + 1);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the LEXB.
***************************************************************************/
void LEXB::MarkMem(void)
{
    AssertValid(0);
    LEXB_PAR::MarkMem();
    MarkMemObj(_pfil);
    MarkMemObj(_pbsf);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Get the current file that we're reading tokens from.
***************************************************************************/
void LEXB::GetStnFile(PSTN pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    *pstn = _stnFile;
}

/** 3DMMv1.0: *************************************************************************
    Fetch some characters.  Don't advance the pointer into the file.  Can
    fetch at most kcchLexbBuf characters at a time.
***************************************************************************/
bool LEXB::_FFetchRgch(achar *prgch, int32_t cch)
{
    AssertThis(0);
    AssertIn(cch, 1, kcchLexbBuf);
    AssertPvCb(prgch, cch * SIZEOF(achar));

    if (_ichLim < _ichCur + cch)
    {
        // 3DMMv1.0: need to read some more data
        int32_t cchT;

        if (_fpCur + (_ichCur + cch - _ichLim) * SIZEOF(achar) > _fpMac)
        {
            // 3DMMv1.0: hit the eof
            return fFalse;
        }

        // 3DMMv1.0: keep any valid characters
        if (_ichCur < _ichLim)
        {
            BltPb(_rgch + _ichCur, _rgch, (_ichLim - _ichCur) * SIZEOF(achar));
            _ichLim -= _ichCur;
        }
        else
            _ichLim = 0;
        _ichCur = 0;

        // 3DMMv1.0: read new stuff
        cchT = LwMin((_fpMac - _fpCur) / SIZEOF(achar), kcchLexbBuf - _ichLim);
        AssertIn(cchT, cch - _ichLim, kcchLexbBuf + 1);
        if (pvNil != _pfil)
        {
            AssertPo(_pfil, 0);
            if (!_pfil->FReadRgb(_rgch + _ichLim, cchT * SIZEOF(achar), _fpCur))
            {
                Warn("Error reading file, truncating logical file");
                _fpMac = _fpCur;
                return fFalse;
            }
        }
        else
        {
            AssertPo(_pbsf, 0);
            _pbsf->FetchRgb(_fpCur, cchT * SIZEOF(achar), _rgch + _ichLim);
        }
        _ichLim += cchT;
        _fpCur += cchT * SIZEOF(achar);
        AssertIn(_ichLim, _ichCur + cch, kcchLexbBuf + 1);
    }

    // 3DMMv1.0: get the text
    CopyPb(_rgch + _ichCur, prgch, cch * SIZEOF(achar));
    AssertThis(0);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Skip any white space at the current location in the buffer.  This
    handles #line directives and comments.  Comments are not allowed on
    the same line as a #line directive.
***************************************************************************/
bool LEXB::_FSkipWhiteSpace(void)
{
    AssertThis(0);
    achar ch;
    bool fStar, fSkipComment, fSlash, fCppMs, fCppGcc;
    int32_t lwLineSav;
    achar rgch[kcchMsvcPoundLine + 1];
    STN stn;

    fSkipComment = fFalse;
    while (_FFetchRgch(&ch))
    {
        if ((_GrfctCh(ch) & fctSpc) || _fSkipToNextLine || fSkipComment)
        {
            _Advance();
#ifdef WIN
            if (kchReturn == ch)
#else
            if (kchLineFeed == ch)
#endif
            {
                _lwLine++;
                _ichLine = 0;
                _fLineStart = fTrue;
                _fSkipToNextLine = fFalse;
            }
            else if (ChLit('\xA') == ch && 1 == _ichLine)
                _ichLine = 0;

            if (fSkipComment)
            {
                if (fStar && ch == ChLit('/'))
                    fSkipComment = fFalse;
                fStar = (ch == ChLit('*'));
            }
            continue;
        }

        // 3DMMv1.0: not a white space character

        // 3DMMv1.0: check for a comment
        if (ChLit('/') == ch && _FFetchRgch(rgch, 2))
        {
            switch (rgch[1])
            {
            case ChLit('/'):
                // 3DMMv1.0: line comment - skip characters until we hit a return
                _Advance(2);
                _fSkipToNextLine = fTrue;
                continue;

            case ChLit('*'):
                // 3DMMv1.0: normal comment
                _Advance(2);
                fSkipComment = fTrue;
                fStar = fFalse;
                continue;
            }
        }

        // 3DMMEx: if this is at the beginning of a line, check for a pre-processor directive
        fCppMs = fFalse;
        fCppGcc = fFalse;
        if (_fLineStart && (ch == _szMsvcPoundLine[0]) && _FFetchRgch(rgch, 2))
        {
            // 3DMMEx: check for #line directive (msvc)
            if (FEqualRgb(rgch, _szMsvcPoundLine, 2) && _FFetchRgch(rgch, kcchMsvcPoundLine + 1) &&
                (_GrfctCh(rgch[kcchMsvcPoundLine]) & fctSpc))
                fCppMs = fTrue;

            // 3DMMEx: check for #<space> directive (gcc)
            if (FEqualRgb(rgch, _szGccPoundLine, 2) && _FFetchRgch(rgch, kcchGccPoundLine) &&
                (_GrfctCh(rgch[kcchGccPoundLine - 1]) & fctSpc))
                fCppGcc = fTrue;

            if (fCppMs == fFalse && fCppGcc == fFalse)
            {
                _fLineStart = fFalse;
                break;
            }
        }
        else
        {
            _fLineStart = fFalse;
            break;
        }

        if (fCppMs == fTrue)
        {
            // 3DMMv1.0: a #line directive - skip it and white space
            _Advance(kcchMsvcPoundLine);
            while (_FFetchRgch(&ch) && (_GrfctCh(ch) & fctSpc) && ch != kchReturn)
                _Advance();

            // 3DMMv1.0: read the line number
            lwLineSav = _lwLine;
            if (!_FFetchRgch(&ch) || !(_GrfctCh(ch) & fctDec))
                goto LBadDirective;
            _Advance();
            _ReadNumber(&_lwLine, ch, 10, klwMax);
            _lwLine--;

            // 3DMMv1.0: skip white space (and make sure there is some)
            if (!_FFetchRgch(&ch))
                break; // 3DMMv1.0: eof
            if (!(_GrfctCh(ch) & fctSpc))
                goto LBadDirective;
            while (_FFetchRgch(&ch) && (_GrfctCh(ch) & fctSpc) && ch != kchReturn)
                _Advance();
            if (!_FFetchRgch(&ch))
                break; // 3DMMv1.0: eof
            if (ch == kchReturn)
                continue; // 3DMMv1.0: end of #line

            // 3DMMv1.0: read file name
            if (ch != ChLit('"'))
                goto LBadDirective;
            _Advance();
            stn.SetNil();
            for (fSlash = fFalse;;)
            {
                if (!_FFetchRgch(&ch) || ch == kchReturn)
                    goto LBadDirective;
                _Advance();
                if (ch == ChLit('"'))
                    break;
                if (ch == ChLit('\\'))
                {
                    // 3DMMv1.0: if this is the second of a pair of slashes, skip it
                    fSlash = !fSlash;
                    if (!fSlash)
                        continue;
                }
                else
                    fSlash = fFalse;
                stn.FAppendCh(ch);
            }

            // 3DMMv1.0: skip white space to end of line
            if (!_FFetchRgch(&ch))
                goto LSetFileName; // 3DMMv1.0: eof
            if (!(_GrfctCh(ch) & fctSpc))
                goto LBadDirective;
            while (_FFetchRgch(&ch) && (_GrfctCh(ch) & fctSpc) && ch != kchReturn)
                _Advance();
            if (!_FFetchRgch(&ch))
                goto LSetFileName; // 3DMMv1.0: eof
            if (ch != kchReturn)
            {
            LBadDirective:
                // 3DMMv1.0: Bad #line directive - restore the line number
                _lwLine = lwLineSav;
                return fFalse;
            }
            else
            {
            LSetFileName:
                _stnFile = stn;
            }
        }

        if (fCppGcc == fTrue)
        {
            // 3DMMEx: a #<space> directive - skip it and white space
            _Advance(kcchGccPoundLine);
            while (_FFetchRgch(&ch) && (_GrfctCh(ch) & fctSpc) && ch != kchReturn && ch != kchLineFeed)
                _Advance();

            // 3DMMv1.0: read the line number
            lwLineSav = _lwLine;
            if (!_FFetchRgch(&ch) || !(_GrfctCh(ch) & fctDec))
                goto LBadDirective;
            _Advance();
            _ReadNumber(&_lwLine, ch, 10, klwMax);
            if (_lwLine)
                _lwLine--;

            // 3DMMv1.0: skip white space (and make sure there is some)
            if (!_FFetchRgch(&ch))
                break; // 3DMMv1.0: eof
            if (!(_GrfctCh(ch) & fctSpc))
                goto LBadDirective;
            while (_FFetchRgch(&ch) && (_GrfctCh(ch) & fctSpc) && ch != kchReturn && ch != kchLineFeed)
                _Advance();
            if (!_FFetchRgch(&ch))
                break; // 3DMMv1.0: eof
            if (ch == kchReturn || ch == kchLineFeed)
                continue; // 3DMMEx: end of #<space>

            // 3DMMv1.0: read file name
            if (ch != ChLit('"'))
                goto LBadDirective;
            _Advance();
            stn.SetNil();
            for (fSlash = fFalse;;)
            {
                if (!_FFetchRgch(&ch) || ch == kchReturn || ch == kchLineFeed)
                    goto LBadDirective2;
                _Advance();
                if (ch == ChLit('"'))
                    break;
                if (ch == ChLit('\\'))
                {
                    // 3DMMv1.0: if this is the second of a pair of slashes, skip it
                    fSlash = !fSlash;
                    if (!fSlash)
                        continue;
                }
                else
                    fSlash = fFalse;
                stn.FAppendCh(ch);
            }

            // 3DMMEx: if end of line, no flags
            if (!_FFetchRgch(&ch))
                goto LSetFileName2; // 3DMMv1.0: eof
            if (!(_GrfctCh(ch) & fctSpc))
                goto LBadDirective2;
            if (ch == kchReturn || ch == kchLineFeed)
            {
                goto LSetFileName2; // 3DMMEx: no flags
            }
            // 3DMMEx: fetch up until end of line
            while (_FFetchRgch(&ch) && ((_GrfctCh(ch) & fctSpc) || (_GrfctCh(ch) & fctDec)) && ch != kchReturn &&
                   ch != kchLineFeed)
                _Advance();
            if (!_FFetchRgch(&ch))
                goto LSetFileName; // 3DMMv1.0: eof

            if (ch != kchReturn && ch != kchLineFeed)
            {
            LBadDirective2:
                // 3DMMEx: Bad #<space> directive - restore the line number
                _lwLine = lwLineSav;
                return fFalse;
            }
            else
            {
            LSetFileName2:
                _stnFile = stn;
            }
        }
    }

    // 3DMMv1.0: if fSkipComment is true, we hit the eof in a comment
    return !fSkipComment;
}

/** 3DMMv1.0: *************************************************************************
    Get the next token from the file.
***************************************************************************/
bool LEXB::FGetTok(PTOK ptok)
{
    AssertThis(0);
    AssertVarMem(ptok);
    achar ch, ch2;
    uint32_t grfct;
    int32_t cch;

    ptok->stn.SetNil();
    if (!_FSkipWhiteSpace())
    {
        _fSkipToNextLine = fTrue;
        goto LError;
    }

    if (!_FFetchRgch(&ch))
    {
        ptok->tt = ttNil;
        return fFalse;
    }
    _Advance();

    grfct = _GrfctCh(ch);
    if (grfct & fctDec)
    {
        // 3DMMv1.0: numeric value
        ptok->tt = ttLong;
        if (ch == ChLit('0'))
        {
            // 3DMMv1.0: hex or octal
            if (!_FFetchRgch(&ch))
            {
                ptok->lw = 0;
                return fTrue;
            }

            if (ch == ChLit('x') || ch == ChLit('X'))
            {
                // 3DMMv1.0: hex
                _Advance();
                if (!_FReadHex(&ptok->lw))
                    goto LError;
            }
            else
            {
                // 3DMMv1.0: octal
                _ReadNumTok(ptok, ChLit('0'), 8, klwMax);
            }
        }
        else
        {
            // 3DMMv1.0: decimal
            _ReadNumTok(ptok, ch, 10, klwMax);
        }

        // 3DMMv1.0: check for bad termination
        if (_FFetchRgch(&ch) && (_GrfctCh(ch) & (fctDec | fctUpp | fctLow | fctQuo)))
        {
            goto LError;
        }
        return fTrue;
    }

    if (grfct & fctQuo)
    {
        // 3DMMv1.0: single or double quote
        if (ch == ChLit('"'))
        {
            // 3DMMv1.0: string
            ptok->tt = ttString;
            for (;;)
            {
                if (!_FFetchRgch(&ch))
                    goto LError;
                _Advance();
                switch (ch)
                {
                case kchReturn:
                    goto LError;
                case ChLit('"'):
                    // 3DMMv1.0: check for another string immediately following this one
                    if (!_fUnionStrings)
                        return fTrue;
                    if (!_FSkipWhiteSpace())
                    {
                        _fSkipToNextLine = fTrue;
                        goto LError;
                    }
                    if (!_FFetchRgch(&ch) || ch != ChLit('"'))
                        return fTrue;
                    _Advance();
                    break;
                case ChLit('\\'):
                    // 3DMMv1.0: control sequence
                    if (!_FReadControlCh(&ch))
                    {
                        _fSkipToNextLine = fTrue;
                        goto LError;
                    }
                    if (chNil != ch)
                        ptok->stn.FAppendCh(ch);
                    break;
                default:
                    ptok->stn.FAppendCh(ch);
                    break;
                }
            }
            Assert(fFalse, "how'd we get here?");
        }

        Assert(ch == ChLit('\''), "bad grfct");
        ptok->tt = ttLong;
        ptok->lw = 0;
        // 3DMMv1.0: ctg type long
        for (cch = 0; cch < 5;)
        {
            if (!_FFetchRgch(&ch))
                goto LError;
            _Advance();
            switch (ch)
            {
            case kchReturn:
                goto LError;
            case ChLit('\''):
                return fTrue;
            case ChLit('\\'):
                if (!_FReadControlCh(&ch))
                {
                    _fSkipToNextLine = fTrue;
                    goto LError;
                }
                break;
            }
            ptok->lw = (ptok->lw << 8) + (uint8_t)ch;
            cch++;
        }
        // 3DMMv1.0: constant too long
        goto LError;
    }

    if (grfct & fctOp1)
    {
        // 3DMMv1.0: check for multi character token
        if (_FFetchRgch(&ch2) && (_GrfctCh(ch2) & fctOp2) && ttNil != (ptok->tt = _TtFromChCh(ch, ch2)))
        {
            _Advance();

            // 3DMMv1.0: special case <<= and >>=
            if ((ptok->tt == ttShr || ptok->tt == ttShl) && _FFetchRgch(&ch2) && ch2 == ChLit('='))
            {
                ptok->tt = (ptok->tt == ttShr) ? ttAShr : ttAShl;
                _Advance();
            }

            return fTrue;
        }
    }

    if (grfct & fctOpr)
    {
        /* 3DMMv1.0: single character token */
        ptok->tt = _TtFromCh(ch);
        Assert(ttNil != ptok->tt, "bad table entry");
        return fTrue;
    }

    if (grfct & (fctLow | fctUpp))
    {
        // 3DMMv1.0: identifier
        ptok->tt = ttName;
        ptok->stn.FAppendCh(ch);
        while (_FFetchRgch(&ch) && (_GrfctCh(ch) & (fctUpp | fctLow | fctDec)))
        {
            ptok->stn.FAppendCh(ch);
            _Advance();
        }
        return fTrue;
    }

LError:
    ptok->tt = ttError;
    ptok->stn.SetNil();

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return the size of extra data associated with the last token returned.
***************************************************************************/
int32_t LEXB::CbExtra(void)
{
    AssertThis(0);
    return 0;
}

/** 3DMMv1.0: *************************************************************************
    Get the extra data for the last token returned.
***************************************************************************/
void LEXB::GetExtra(void *pv)
{
    AssertThis(0);
    Bug("no extra data");
}

/** 3DMMv1.0: *************************************************************************
    Read a number.  The first character is passed in ch.  lwBase is the base
    of the number (must be <= 10).
***************************************************************************/
void LEXB::_ReadNumber(int32_t *plw, achar ch, int32_t lwBase, int32_t cchMax)
{
    AssertThis(0);
    AssertVarMem(plw);
    AssertIn(ch - ChLit('0'), 0, lwBase);
    AssertIn(lwBase, 2, 11);

    *plw = ch - ChLit('0');
    while (--cchMax > 0 && _FFetchRgch(&ch) && (_GrfctCh(ch) & fctDec) && (ch - ChLit('0') < lwBase))
    {
        *plw = *plw * lwBase + (ch - ChLit('0'));
        _Advance();
    }
}

/** 3DMMv1.0: *************************************************************************
    Read in a hexadecimal value (without the 0x).
***************************************************************************/
bool LEXB::_FReadHex(int32_t *plw)
{
    AssertThis(0);
    AssertVarMem(plw);
    achar ch;
    uint32_t grfct;

    *plw = 0;
    if (!_FFetchRgch(&ch) || !((grfct = _GrfctCh(ch)) & fctHex))
        return fFalse;

    do
    {
        if (grfct & fctDec)
            *plw = *plw * 16 + (ch - ChLit('0'));
        else if (grfct & fctLow)
            *plw = *plw * 16 + (10 + ch - ChLit('a'));
        else
        {
            Assert(grfct & fctUpp, "bad grfct");
            *plw = *plw * 16 + (10 + ch - ChLit('A'));
        }
        _Advance();
    } while (_FFetchRgch(&ch) && ((grfct = _GrfctCh(ch)) & fctHex));

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read a control character (eg, \x3F).  This code assumes the \ has
    already been read.
***************************************************************************/
bool LEXB::_FReadControlCh(achar *pch)
{
    AssertThis(0);
    AssertVarMem(pch);
    // 3DMMv1.0: control sequence
    achar ch;
    int32_t lw;

    if (!_FFetchRgch(&ch))
        return fFalse;
    _Advance();

    switch (ch)
    {
    case kchReturn:
        while (_FFetchRgch(&ch) && ch == ChLit('\xA'))
            _Advance();
        *pch = chNil;
        break;
    case ChLit('t'):
        *pch = kchTab;
        break;
    case ChLit('n'):
        *pch = kchReturn;
        break;
    case ChLit('x'):
    case ChLit('X'):
        if (!_FReadHex(&lw))
            return fFalse;
        *pch = (achar)lw;
        break;
    default:
        if (_GrfctCh(ch) & fctOct)
        {
            _ReadNumber(&lw, ch, 8, 3);
            *pch = (achar)lw;
        }
        else
            *pch = ch;
        break;
    }
    return fTrue;
}
