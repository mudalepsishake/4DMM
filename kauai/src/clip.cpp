/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Clipboard object implementation.

***************************************************************************/
#include "frame.h"
ASSERTNAME

RTCLASS(CLIP)

PCLIP vpclip;
CLIP _clip;

/** 3DMMv1.0: *************************************************************************
    Constructor for the clipboard.
***************************************************************************/
CLIP::CLIP(void)
{
    vpclip = this;
    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Return whether the given document is the clipboard document.  If pdocb
    is nil this returns true if the clipboard is empty and false if it's
    not empty.
***************************************************************************/
bool CLIP::FDocIsClip(PDOCB pdocb)
{
    AssertThis(0);
    AssertNilOrPo(pdocb, 0);

    if (pvNil == pdocb)
        _EnsureDoc();

    return pdocb == _pdocb;
}

/** 3DMMv1.0: *************************************************************************
    Show the clipboard document.
***************************************************************************/
void CLIP::Show(void)
{
    AssertThis(0);

    _EnsureDoc();
    _ImportCur();
    if (pvNil != _pdocb)
    {
        if (_pdocb->Cddg() > 0)
            _pdocb->ActivateDmd();
        else
            _pdocb->PdmdNew();
    }
}

/** 3DMMv1.0: *************************************************************************
    Make the given document the clipboard document.
***************************************************************************/
void CLIP::Set(PDOCB pdocb, bool fExport)
{
    AssertThis(0);
    AssertNilOrPo(pdocb, 0);

    if (_fExporting || _fImporting)
    {
        Bug("can't change the clipboard while exporting or importing");
        return;
    }

    if (pdocb == _pdocb)
        return;

    _fDocCurrent = fTrue;
    _fDelayImport = fFalse;
    _clfmImport = clfmNil;

    // 3DMMv1.0: throw away the old clip document and replace it with the new one
    SwapVars(&pdocb, &_pdocb);
    if (pvNil != pdocb)
    {
        if (pdocb->Cddg() > 0)
            pdocb->UpdateName();
        ReleasePpo(&pdocb);
    }

    if (pvNil != _pdocb)
    {
        _pdocb->AddRef();
        if (_pdocb->Cddg() > 0)
            _pdocb->UpdateName();

        if (fExport)
        {
            _pdocb->ExportFormats(this);
            EndExport();
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    See if the clipboard supports this format and if so, get it.
***************************************************************************/
bool CLIP::FGetFormat(int32_t cls, PDOCB *ppdocb)
{
    AssertThis(0);
    AssertNilOrVarMem(ppdocb);

    _EnsureDoc();
    if (pvNil != ppdocb)
        _ImportCur();

    if (pvNil == _pdocb)
        return fFalse;

    if (_pdocb->FIs(cls))
    {
        if (pvNil != ppdocb)
        {
            *ppdocb = _pdocb;
            (*ppdocb)->AddRef();
        }
        return fTrue;
    }

    return _pdocb->FGetFormat(cls, ppdocb);
}

/** 3DMMv1.0: *************************************************************************
    Import stuff from the external clipboard.
***************************************************************************/
void CLIP::Import(void)
{
    AssertThis(0);

    if (pvNil != _pdocb && _pdocb->Cddg() > 0)
        Set();
    _fDocCurrent = fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Make sure the _pdocb is current - import the current system clipboard
    if it isn't.
***************************************************************************/
void CLIP::_EnsureDoc(void)
{
    if (_fExporting || _fImporting)
    {
        Bug("can't import the clipboard while exporting or importing");
        return;
    }

    if (_fDocCurrent)
        return;

    _fDocCurrent = fTrue;

#ifdef WIN
    HN hn;
    PDOCB pdocb;
    bool fDelay;
    int32_t clfm;

    if (GetClipboardOwner() == vwig.hwndApp || !OpenClipboard(vwig.hwndApp))
        return;

    for (clfm = 0; 0 != (clfm = EnumClipboardFormats(clfm));)
    {
        if (!_FImportFormat(clfm))
            continue;

        if (hNil == (hn = GetClipboardData(clfm)))
            continue;

        pdocb = pvNil;
        fDelay = fFalse;
        if (_FImportFormat(clfm, GlobalLock(hn), GlobalSize(hn), &pdocb, &fDelay))
        {
            GlobalUnlock(hn);
            Set(pdocb, fFalse);
            _fDelayImport = FPure(fDelay) && pdocb != pvNil;
            _clfmImport = clfm;
            ReleasePpo(&pdocb);
            break;
        }
        GlobalUnlock(hn);
    }
    CloseClipboard();
#endif // 3DMMv1.0: WIN

#ifdef MAC
    RawRtn(); // 3DMMv1.0: REVIEW shonk: Mac: implement CLIP::Import
#endif        // 3DMMv1.0: MAC
}

/** 3DMMv1.0: *************************************************************************
    Import the actual data for the current clipboard (if importing was
    delayed).
***************************************************************************/
void CLIP::_ImportCur(void)
{
    AssertThis(0);
    Assert(_fDocCurrent, 0);

    if (pvNil == _pdocb || !_fDelayImport || clfmNil == _clfmImport)
        return;

#ifdef WIN
    HN hn;
    bool fRet;
    PDOCB pdocb;

    if (GetClipboardOwner() == vwig.hwndApp || !OpenClipboard(vwig.hwndApp))
        return;

    if (hNil != (hn = GetClipboardData(_clfmImport)))
    {
        pdocb = _pdocb;
        if (pvNil != pdocb)
            pdocb->AddRef();
        fRet = _FImportFormat(_clfmImport, GlobalLock(hn), GlobalSize(hn), &pdocb);
        GlobalUnlock(hn);
        if (fRet)
            Set(pdocb, fFalse);
        _fDelayImport = fFalse;
        _clfmImport = clfmNil;
        ReleasePpo(&pdocb);
    }
    CloseClipboard();
#endif // 3DMMv1.0: WIN

#ifdef MAC
    RawRtn(); // 3DMMv1.0: REVIEW shonk: Mac: implement CLIP::_ImportCur
#endif        // 3DMMv1.0: MAC
}

/** 3DMMv1.0: *************************************************************************
    Import a particular format.
***************************************************************************/
bool CLIP::_FImportFormat(int32_t clfm, void *pv, int32_t cb, PDOCB *ppdocb, bool *pfDelay)
{
    AssertThis(0);
    AssertPvCb(pv, cb);
    AssertNilOrVarMem(ppdocb);
    AssertNilOrVarMem(pfDelay);
    Assert(_fDocCurrent, 0);
    bool fRet;

#ifdef WIN
    if (pvNil != pv && cb > 0)
    {
        // 3DMMv1.0: adjust cb for text - remove the stupid trailing null and compensate
        // 3DMMv1.0: for the fact that cb might be too big (GlobalSize(hn) might be bigger
        // 3DMMv1.0: than what was allocated).
        switch (clfm)
        {
        case kclfmUniText:
            wchar *pchw, *pchwLim;

            pchw = (wchar *)pv;
            pchwLim = (wchar *)pv + cb / SIZEOF(wchar);
            while (pchw < pchwLim && *pchw != 0)
                pchw++;
            cb = BvSubPvs(pchw, pv);
            break;

        case kclfmSbText:
            schar *pchs, *pchsLim;

            pchs = (schar *)pv;
            pchsLim = (schar *)pv + cb;
            while (pchs < pchsLim && *pchs != 0)
                pchs++;
            cb = BvSubPvs(pchs, pv);
            break;
        }
    }
#endif // 3DMMv1.0: WIN

    _fImporting = fTrue;
    fRet = vpappb->FImportClip(clfm, pv, cb, ppdocb, pfDelay);
    _fImporting = fFalse;

    return fRet;
}

/** 3DMMv1.0: *************************************************************************
    Start an export session.
***************************************************************************/
bool CLIP::FInitExport(void)
{
    AssertThis(0);

    if (_fExporting || _fImporting)
    {
        Bug("Already exporting or importing");
        return fFalse;
    }

#ifdef WIN
    _hnExport = hNil;

    if (!OpenClipboard(vwig.hwndApp))
        return fFalse;
    if (!EmptyClipboard())
    {
        CloseClipboard();
        return fFalse;
    }
    _fExporting = fTrue;
#endif // 3DMMv1.0: WIN

#ifdef MAC
    RawRtn(); // 3DMMv1.0: REVIEW shonk: Mac: implement CLIP::FInitExport
#endif        // 3DMMv1.0: MAC

    return _fExporting;
}

/** 3DMMv1.0: *************************************************************************
    Allocate a buffer to export to.
***************************************************************************/
void *CLIP::PvExport(int32_t cb, int32_t clfm)
{
    AssertThis(0);
    AssertIn(cb, 1, kcbMax);

    if (!_fExporting)
    {
        Bug("not exporting");
        return pvNil;
    }

    _ExportCur();

#ifdef WIN
    switch (clfm)
    {
    case kclfmText:
        // 3DMMEx: need to add SIZEOF(achar) for the terminating zero character
        cb = LwRoundAway(cb + SIZEOF(achar), SIZEOF(achar));
        break;
    }

    if (hNil == (_hnExport = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE | GMEM_ZEROINIT, cb)))
    {
        return pvNil;
    }

    _clfmExport = clfm;
    return GlobalLock(_hnExport);
#endif // 3DMMv1.0: WIN

#ifdef MAC
    RawRtn(); // 3DMMv1.0: REVIEW shonk: Mac: implement CLIP::PvExport
    return pvNil;
#endif // 3DMMv1.0: MAC
}

/** 3DMMv1.0: *************************************************************************
    End an exporting session.
***************************************************************************/
void CLIP::EndExport(void)
{
    AssertThis(0);

    if (!_fExporting)
        return;

    _ExportCur();

#ifdef WIN
    CloseClipboard();
#endif // 3DMMv1.0: WIN

#ifdef MAC
    RawRtn(); // 3DMMv1.0: REVIEW shonk: Mac: implement CLIP::EndExport
#endif        // 3DMMv1.0: MAC
    _fExporting = fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Export the current format.
***************************************************************************/
void CLIP::_ExportCur(void)
{
    AssertThis(0);
    Assert(_fExporting, 0);

#ifdef WIN
    if (hNil != _hnExport)
    {
        GlobalUnlock(_hnExport);
        SetClipboardData(_clfmExport, _hnExport);
        _hnExport = hNil;
    }
#endif // 3DMMv1.0: WIN

#ifdef MAC
    RawRtn(); // 3DMMv1.0: REVIEW shonk: Mac: implement CLIP::_ExportCur
#endif        // 3DMMv1.0: MAC
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a CLIP.
***************************************************************************/
void CLIP::AssertValid(uint32_t grf)
{
    CLIP_PAR::AssertValid(0);
    AssertNilOrPo(_pdocb, 0);
    Assert(!_fExporting || !_fImporting, "both importing and exporting!");
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the CLIP.
***************************************************************************/
void CLIP::MarkMem(void)
{
    AssertValid(0);
    CLIP_PAR::MarkMem();
    MarkMemObj(_pdocb);
}
#endif // 3DMMv1.0: DEBUG
