/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    File name management.

***************************************************************************/
#include "util.h"
#include <folders.h>
ASSERTNAME

// 3DMMv1.0: This is the FTG to use for temp files - clients may set this to whatever
// 3DMMv1.0: they want.
FTG vftgTemp = kftgTemp;

/** 3DMMv1.0: *************************************************************************
    Implementation notes:  The _fss is a standard FSSpec record and is
    always filled in via a call to FSMakeFSSpec.  If the fni is a directory,
    _ftg will be kftgDir.  For files, _lwDir is the same as _fss.parID.
    For directories, _lwDir is the directory id for the directory described
    by the fni (while _fss.parID is the id of its parent).
***************************************************************************/

typedef StandardFileReply SFR;
priv bool _FFssDir(FSS *pfss, int32_t *plwDir);

RTCLASS(FNI)
RTCLASS(FNE)

/** 3DMMv1.0: *************************************************************************
    Sets the fni to nil values.
***************************************************************************/
void FNI::SetNil(void)
{
    _ftg = ftgNil;
    _lwDir = 0;
    ClearPb(&_fss, size(_fss));
    AssertThis(ffniEmpty);
}

/** 3DMMv1.0: *************************************************************************
    Constructor for fni class.
***************************************************************************/
FNI::FNI(void)
{
    SetNil();
}

/** 3DMMv1.0: *************************************************************************
    Get an fni (for opening) from the user.
***************************************************************************/
bool FNI::FGetOpen(FTG *prgftg, short cftg)
{
    AssertThis(0);
    AssertNilOrVarMem(prgftg);

    SFR sfr;

    StandardGetFile(pvNil, cftg <= 0 ? -1 : cftg, (uint32_t *)prgftg, &sfr);
    if (sfr.sfGood)
    {
        _fss = sfr.sfFile;
        _ftg = sfr.sfType;
        _lwDir = _fss.parID;
        AssertThis(ffniFile);
    }
    else
        SetNil();

    return sfr.sfGood;
}

/** 3DMMv1.0: *************************************************************************
    Get an fni (for saving) from the user.
***************************************************************************/
bool FNI::FGetSave(FTG ftg, PST pstPrompt, PST pstDefault)
{
    AssertThis(0);
    AssertNilOrSt(pstPrompt);
    AssertNilOrSt(pstDefault);

    SFR sfr;

    StandardPutFile((uint8_t *)pstPrompt, (uint8_t *)pstDefault, &sfr);
    if (sfr.sfGood)
    {
        _fss = sfr.sfFile;
        _ftg = ftg;
        _lwDir = _fss.parID;
        AssertThis(ffniFile);
    }
    else
        SetNil();

    return sfr.sfGood;
}

/** 3DMMv1.0: *************************************************************************
    Get a unique filename in the given directory.
***************************************************************************/
bool FNI::FGetUnique(FTG ftg)
{
    AssertThis(ffniFile | ffniDir);
    static int32_t _dlw = 0;
    int32_t lw;
    short cact;
    short err;
    STN stn;
    int32_t lwDir = _lwDir;
    short swVol = _fss.vRefNum;

    Assert(ftg != kftgDir && ftg != ftgNil, "bad ftg");
    lw = TsSystemCurrent() + ++_dlw;
    for (cact = 0; cact < 20; cact++, lw += ++_dlw)
    {
        AssertDo(stn.FFormatSz(PszLit("~TempFile#%08x"), lw), 0);
        err = FSMakeFSSpec(swVol, lwDir, &stn, &_fss);
        if (err == fnfErr)
        {
            _ftg = ftg;
            _lwDir = _fss.parID;
            AssertThis(ffniFile);
            return fTrue;
        }
    }
    SetNil();
    PushErc(ercFniGeneral);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Get a temporary fni.
***************************************************************************/
bool FNI::FGetTemp(void)
{
    AssertThis(0);
    static short _swVolTemp = 0;
    static int32_t _lwDirTemp = -1;

    // 3DMMv1.0: This is so we only call FindFolder once.
    if (_swVolTemp == 0 && _lwDirTemp == -1 &&
        FindFolder(0, kTemporaryFolderType, kCreateFolder, &_swVolTemp, &_lwDirTemp) != noErr)
    {
        _swVolTemp = 0;
        _lwDirTemp = 0;
    }

    if (FSMakeFSSpec(_swVolTemp, _lwDirTemp, pvNil, &_fss) != noErr)
    {
        SetNil();
        PushErc(ercFniGeneral);
        return fFalse;
    }

    _ftg = kftgDir;
    _lwDir = _lwDirTemp;
    return FGetUnique(vftgTemp);
}

/** 3DMMv1.0: *************************************************************************
    Return the file type of the fni.
***************************************************************************/
FTG FNI::Ftg(void)
{
    AssertThis(0);
    return _ftg;
}

/** 3DMMv1.0: *************************************************************************
    Return the volume kind for the given fni.
***************************************************************************/
uint32_t FNI::Grfvk(void)
{
    AssertThis(0);
    uint32_t grfvk = fvkNil;

    // 3DMMv1.0: REVIEW shonk: Mac Grfvk: implement
    RawRtn();
    return grfvk;
}

/** 3DMMv1.0: *************************************************************************
    Set the leaf for the fni.
***************************************************************************/
bool FNI::FSetLeaf(PSTZ pstz, FTG ftg)
{
    AssertThis(ffniFile | ffniDir);
    return FBuild(_fss.vRefNum, _lwDir, pstz, ftg);
}

/** 3DMMv1.0: *************************************************************************
    Set the leaf for the fni.
***************************************************************************/
bool FNI::FBuild(int32_t lwVol, int32_t lwDir, PSTZ pstz, FTG ftg)
{
    AssertNilOrStz(pstz);
    short err;
    FSS fss;

    Assert(FPure(ftg == kftgDir) == FPure(pstz == pvNil || CchStz(pstz) == 0), "pstz doesn't match ftg");
    if (ftg == kftgDir)
        pstz = pvNil;

    err = FSMakeFSSpec((short)lwVol, lwDir, (uint8_t *)pstz, &fss);
    if (ftg == kftgDir)
    {
        // 3DMMv1.0: a directory (it had better exist)
        if (noErr != err)
            goto LFail;
        _lwDir = lwDir;
    }
    else
    {
        // 3DMMv1.0: not supposed to be a directory - so make sure it isn't
        if (fnfErr != err)
        {
            if (noErr != err)
                goto LFail;
            if (_FFssDir(&fss, pvNil))
                goto LFail;
        }
        _lwDir = fss.parID;
    }
    _fss = fss;
    _ftg = ftg;
    AssertThis(ffniFile | ffniDir);
    return fTrue;

LFail:
    SetNil();
    PushErc(ercFniGeneral);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Set the FTG for the fni.
***************************************************************************/
bool FNI::FChangeFtg(FTG ftg)
{
    AssertThis(ffniFile);
    Assert(ftg != ftgNil && ftg != kftgDir, "Bad FTG");

    _ftg = ftg;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Get the leaf name for the fni.
***************************************************************************/
void FNI::GetLeaf(PSTZ pstz)
{
    AssertThis(0);
    AssertMaxStz(pstz);

    if (_ftg == kftgDir)
        SetStzNil(pstz);
    else
        CopyStStz((achar *)_fss.name, pstz);
}

/** 3DMMv1.0: *************************************************************************
    Get a string representing the path of the fni.
***************************************************************************/
void FNI::GetStzPath(PSTZ pstz)
{
    AssertThis(0);
    AssertMaxStz(pstz);
    RawRtn(); // 3DMMv1.0: REVIEW shonk: Mac GetStzPath: implement for real
    CopyStStz((achar *)_fss.name, pstz);
}

/** 3DMMv1.0: *************************************************************************
    Determines if the file/directory exists.  Returns tMaybe on error or
    if the fni type (file or dir) doesn't match the disk object of the
    same name or if the file/dir is invisible or is an alias.  Pushes an
    erc if it returns tMaybe.
***************************************************************************/
bool FNI::TExists(void)
{
    AssertThis(ffniFile | ffniDir);
    CInfoPBRec iob;
    short err;
    achar st[kcbMaxSt];

    ClearPb(&iob, size(iob));
    CopySt((achar *)_fss.name, st);
    iob.hFileInfo.ioNamePtr = (uint8_t *)st;
    iob.hFileInfo.ioVRefNum = _fss.vRefNum;
    iob.hFileInfo.ioDirID = _fss.parID;
    if ((err = PBGetCatInfo(&iob, fFalse)) != noErr)
    {
        if (err != fnfErr)
        {
            PushErc(ercFniGeneral);
            return tMaybe;
        }
        return tNo;
    }
    if ((_ftg == kftgDir) != FPure(iob.hFileInfo.ioFlAttrib & 0x0010))
    {
        PushErc(ercFniMismatch);
        return tMaybe;
    }
    if (iob.hFileInfo.ioFlFndrInfo.fdFlags & (kIsInvisible | kIsAlias))
    {
        PushErc(ercFniHidden);
        return tMaybe;
    }
    return tYes;
}

/** 3DMMv1.0: *************************************************************************
    Delete the file.
***************************************************************************/
bool FNI::FDelete(void)
{
    AssertThis(ffniFile);
    if (FSpDelete(&_fss) == noErr)
        return fTrue;
    PushErc(ercFniDelete);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Rename the file as indicated by *pfni.  The directories must match.
***************************************************************************/
bool FNI::FRename(FNI *pfni)
{
    AssertThis(ffniFile);
    AssertPo(pfni, ffniFile);
    Assert(_fss.vRefNum == pfni->_fss.vRefNum && _fss.parID == pfni->_fss.parID, "directory change");
    Assert(_ftg == pfni->_ftg, "ftg's don't match");

    if (FSpRename(&_fss, pfni->_fss.name) == noErr)
        return fTrue;
    PushErc(ercFniRename);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Compare two fni's for equality.  Doesn't consider the ftg's.
***************************************************************************/
bool FNI::FEqual(FNI *pfni)
{
    // 3DMMv1.0: NOTE: see IM:Text, pg 5-17.  It's not documented whether the comparison
    // 3DMMv1.0: should be case sensitive and/or diacritical sensitive.  Experimenting
    // 3DMMv1.0: with the US finder indicates that we should use case insensitive,
    // 3DMMv1.0: diacritical sensitive comparison.
    AssertThis(ffniFile | ffniDir);
    AssertPo(pfni, ffniFile | ffniDir);

    return pfni->_fss.vRefNum == _fss.vRefNum && pfni->_fss.parID == _fss.parID &&
           EqualString(pfni->_fss.name, _fss.name, fFalse, fTrue);
}

/** 3DMMv1.0: *************************************************************************
    Return whether the fni refers to a directory.
***************************************************************************/
bool FNI::FDir(void)
{
    AssertThis(0);
    return _ftg == kftgDir;
}

/** 3DMMv1.0: *************************************************************************
    Return whether the directory portions of the fni's are the same.
***************************************************************************/
bool FNI::FSameDir(FNI *pfni)
{
    AssertThis(ffniDir | ffniFile);
    AssertPo(pfni, ffniDir | ffniFile);

    return pfni->_fss.vRefNum == _fss.vRefNum && pfni->_lwDir == _lwDir;
}

/** 3DMMv1.0: *************************************************************************
    Determine if the directory pstz in fni exists, optionally creating it
    and/or moving into it.  Specify ffniCreateDir to create it if it
    doesn't exist.  Specify ffniMoveTo to make the fni refer to it.
    If this fails, the fni is untouched.
***************************************************************************/
bool FNI::FDownDir(PSTZ pstz, uint32_t grffni)
{
    AssertThis(ffniDir);
    AssertStz(pstz);
    FSS fss;
    int32_t lwDir;
    short err;

    // 3DMMv1.0: make the fss
    err = FSMakeFSSpec(_fss.vRefNum, _lwDir, (uint8_t *)pstz, &fss);
    if (noErr == err)
    {
        // 3DMMv1.0: exists, make sure it is a directory and get the directory id
        if (!_FFssDir(&fss, &lwDir))
        {
            PushErc(ercFniMismatch);
            return fFalse;
        }
    }
    else
    {
        if (fnfErr != err)
        {
            PushErc(ercFniGeneral);
            return fFalse;
        }

        // 3DMMv1.0: doesn't exist
        if (!(grffni & ffniCreateDir))
            return fFalse;

        // 3DMMv1.0: create it
        err = FSpDirCreate(&fss, smSystemScript, &lwDir);
        if (err != noErr)
        {
            PushErc(ercFniDirCreate);
            return fFalse;
        }
    }

    if (grffni & ffniMoveToDir)
    {
        _fss = fss;
        _lwDir = lwDir;
        _ftg = kftgDir;
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Gets the lowest directory name (if pstz is not nil) and optionally
    moves the fni up a level (if ffniMoveToDir is specified).  If this
    fails, the fni is untouched.
***************************************************************************/
bool FNI::FUpDir(PSTZ pstz, uint32_t grffni)
{
    AssertThis(ffniDir);
    AssertNilOrMaxStz(pstz);

    CInfoPBRec iob;
    short err;
    FSS fss;

    if (_lwDir == fsRtDirID)
        return fFalse;

    ClearPb(&iob, size(iob));
    iob.dirInfo.ioFDirIndex = -1; // 3DMMv1.0: ignore name, look at vol/dir
    iob.dirInfo.ioNamePtr = (uint8_t *)pstz;
    iob.dirInfo.ioVRefNum = _fss.vRefNum;
    iob.dirInfo.ioDrDirID = _lwDir;
    if (PBGetCatInfo(&iob, fFalse) != noErr)
    {
        PushErc(ercFniGeneral);
        return fFalse;
    }
    if (pstz != pvNil)
        SetStzCch(pstz, CchSt(pstz));
    Assert(iob.dirInfo.ioFlAttrib & 0x0010, "not a directory?!");
    if (grffni & ffniMoveToDir)
    {
        err = FSMakeFSSpec(_fss.vRefNum, iob.dirInfo.ioDrParID, pvNil, &fss);
        if (noErr != err)
        {
            PushErc(ercFniGeneral);
            return fFalse;
        }
        _fss = fss;
        _lwDir = iob.dirInfo.ioDrParID;
        _ftg = kftgDir;
        AssertThis(ffniDir);
    }
    return fTrue;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert validity of the FNI.
***************************************************************************/
void FNI::AssertValid(uint32_t grffni)
{
    FNI_PAR::AssertValid(0);
    if (grffni == 0)
        grffni = ffniEmpty | ffniFile | ffniDir;

    if (_ftg == ftgNil)
    {
        Assert(grffni & ffniEmpty, "unexpected nil fni");
        Assert(_fss.vRefNum == 0 && _fss.parID == 0 && _lwDir == 0 && CchSt((achar *)_fss.name) == 0, "bad nil");
    }
    else if (_ftg == kftgDir)
    {
        Assert(grffni & ffniDir, "unexpected dir");
        Assert(_lwDir != _fss.parID, "dir is its own parent?");
    }
    else
    {
        Assert(grffni & ffniFile, "unexpected file");
        Assert(CchSt((achar *)_fss.name) > 0, "no name for file");
        Assert(_lwDir == _fss.parID, "parent not consistent?");
    }
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Low level routine to determine if the given fss points to an existing
    directory.  If so and plwDir is not nil, sets *plwDir to the id of
    the directory.
***************************************************************************/
priv bool _FFssDir(FSS *pfss, int32_t *plwDir)
{
    CInfoPBRec iob;

    // 3DMMv1.0: find the entry
    ClearPb(&iob, size(iob));
    iob.hFileInfo.ioNamePtr = (StringPtr)pfss->name;
    iob.hFileInfo.ioVRefNum = pfss->vRefNum;
    iob.hFileInfo.ioDirID = pfss->parID;
    if (PBGetCatInfo(&iob, fFalse) != noErr)
    {
        PushErc(ercFniGeneral);
        goto LFail;
    }

    // 3DMMv1.0: entry exists so see if it's a directory
    if (!(iob.hFileInfo.ioFlAttrib & 0x0010))
    {
    LFail:
        TrashVar(plwDir);
        return fFalse;
    }

    if (plwDir != pvNil)
        *plwDir = iob.dirInfo.ioDrDirID;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Constructor for a File Name Enumerator.
***************************************************************************/
FNE::FNE(void)
{
    AssertBaseThis(0);
    _prgftg = _rgftg;
    _pglfes = pvNil;
    _fInited = fFalse;
    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Destructor for an FNE.
***************************************************************************/
FNE::~FNE(void)
{
    AssertBaseThis(0);
    _Free();
}

/** 3DMMv1.0: *************************************************************************
    Free all the memory associated with the FNE.
***************************************************************************/
void FNE::_Free(void)
{
    if (_prgftg != _rgftg)
    {
        FreePpv((void **)&_prgftg);
        _prgftg = _rgftg;
    }
    ReleasePpo(&_pglfes);
    _fInited = fFalse;
    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Initialize the fne to do an enumeration.
***************************************************************************/
bool FNE::FInit(FNI *pfniDir, FTG *prgftg, int32_t cftg, uint32_t grffne)
{
    AssertThis(0);
    AssertNilOrVarMem(pfniDir);
    AssertIn(cftg, 0, kcbMax);
    AssertPvCb(prgftg, LwMul(cftg, size(FTG)));

    // 3DMMv1.0: free the old stuff
    _Free();

    if (0 >= cftg)
        _cftg = 0;
    else
    {
        int32_t cb = LwMul(cftg, size(FTG));

        if (cftg > kcftgFneBase && !FAllocPv((void **)&_prgftg, cb, fmemNil, mprNormal))
        {
            _prgftg = _rgftg;
            PushErc(ercFneGeneral);
            AssertThis(0);
            return fFalse;
        }
        CopyPb(prgftg, _prgftg, cb);
        _cftg = cftg;
    }

    if (pfniDir == pvNil)
        _fesCur.lwVol = 0;
    else
    {
        _fesCur.lwVol = (int32_t)pfniDir->_fss.vRefNum;
        _fesCur.lwDir = pfniDir->_lwDir;
    }
    _fesCur.iv = 0;
    _fRecurse = FPure(grffne & ffneRecurse);
    _fInited = fTrue;
    AssertThis(0);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Get the next FNI in the enumeration.
***************************************************************************/
bool FNE::FNextFni(FNI *pfni, uint32_t *pgrffneOut, uint32_t grffneIn)
{
    AssertThis(0);
    AssertVarMem(pfni);
    AssertNilOrVarMem(pgrffneOut);
    short err;

    if (!_fInited)
    {
        Bug("must initialize the FNE before using it!");
        return fFalse;
    }

    if (grffneIn & ffneSkipDir)
    {
        // 3DMMv1.0: skip the rest of the stuff in this dir
        if (pvNil == _pglfes || !_pglfes->FPop(&_fesCur))
            goto LDone;
    }

    if (_fesCur.lwVol == 0)
    {
        // 3DMMv1.0: volume
        ParamBlockRec iob;

        do
        {
            ClearPb(&iob, size(iob));
            iob.volumeParam.ioVolIndex = (short)++_fesCur.iv;
            if ((err = PBGetVInfoSync(&iob)) != noErr)
            {
                if (err != nsvErr)
                    PushErc(ercFneGeneral);
                goto LDone;
            }
        } while (!pfni->FBuild(iob.volumeParam.ioVRefNum, 0, pvNil, kftgDir));

        // 3DMMv1.0: we've got one
        goto LGotOne;
    }

    // 3DMMv1.0: directory or file
    for (;;)
    {
        int ich;
        bool fT;
        FTG *pftg;
        PSZ pszExt;
        CInfoPBRec iob;
        achar stz[kcbMaxStz];

        ClearPb(&iob, size(iob));
        iob.hFileInfo.ioNamePtr = (uint8_t *)stz;
        iob.hFileInfo.ioVRefNum = (short)_fesCur.lwVol;
        iob.hFileInfo.ioDirID = _fesCur.lwDir;
        iob.hFileInfo.ioFDirIndex = (short)++_fesCur.iv;
        if ((err = PBGetCatInfoSync(&iob)) != noErr)
        {
            if (err != fnfErr)
                PushErc(ercFneGeneral);
            goto LPop;
        }

        if (iob.hFileInfo.ioFlFndrInfo.fdFlags & (kIsInvisible | kNameLocked | kIsAlias))
        {
            continue;
        }

        if (iob.hFileInfo.ioFlAttrib & 0x0010)
            fT = pfni->FBuild(_fesCur.lwVol, iob.hFileInfo.ioDirID, pvNil, kftgDir);
        else
        {
            StToStz(stz);
            fT = pfni->FBuild(_fesCur.lwVol, _fesCur.lwDir, stz, iob.hFileInfo.ioFlFndrInfo.fdType);
        }
        if (!fT)
            continue;

        if (_cftg == 0)
            goto LGotOne;
        for (pftg = _prgftg + _cftg; pftg-- > _prgftg;)
        {
            if (*pftg == pfni->_ftg)
                goto LGotOne;

            // 3DMMv1.0: see if the file has a dos-like extension matching the ftg
            if (pfni->_ftg != kftgDir && (pszExt = (achar *)pftg)[3] == 0 && (ich = CchStz(stz) - CchSz(pszExt)) > 0 &&
                PszStz(stz)[ich - 1] == '.' && fcmpEq == FcmpCompareInsSz(PszStz(stz) + ich, pszExt))
            {
                goto LGotOne;
            }
        }
    }
    Bug("How did we fall through to here?");

LPop:
    if (_pglfes == pvNil || _pglfes->IvMac() == 0)
        goto LDone;

    // 3DMMv1.0: we're about to pop a directory, so send the current directory back
    // 3DMMv1.0: with ffnePost
    if (pvNil != pgrffneOut)
        *pgrffneOut = ffnePost;
    if (!pfni->FBuild(_fesCur.lwVol, _fesCur.lwDir, pvNil, kftgDir))
    {
        PushErc(ercFneGeneral);
    LDone:
        _Free();
        AssertThis(0);
        return fFalse;
    }
    Assert(_pglfes->FPop(&_fesCur), 0);
    AssertPo(pfni, ffniDir);
    AssertThis(0);
    return fTrue;

LGotOne:
    AssertPo(pfni, ffniFile | ffniDir);
    if (pvNil != pgrffneOut)
        *pgrffneOut = ffnePre | ffnePost;

    if (_fRecurse && pfni->_ftg == kftgDir)
    {
        if ((pvNil != _pglfes || pvNil != (_pglfes = GL::PglNew(size(FES), 5))) && _pglfes->FPush(&_fesCur))
        {
            // 3DMMv1.0: set up the new fes
            _fesCur.lwVol = pfni->_fss.vRefNum;
            _fesCur.lwDir = pfni->_lwDir;
            _fesCur.iv = 0;
            if (pvNil != pgrffneOut)
                *pgrffneOut = ffnePre;
        }
        else
            PushErc(ercFneGeneral);
    }
    AssertThis(0);
    return fTrue;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a FNE.
***************************************************************************/
void FNE::AssertValid(uint32_t grf)
{
    FNE_PAR::AssertValid(0);
    if (_fInited)
    {
        AssertNilOrPo(_pglfes, 0);
        AssertIn(_cftg, 0, kcbMax);
        AssertPvCb(_prgftg, LwMul(size(FTG), _cftg));
        Assert((_cftg > kcftgFneBase) == (_prgftg == _rgftg), "wrong _prgftg");
    }
    else
        Assert(_pglfes == pvNil, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the FNE.
***************************************************************************/
void FNE::MarkMem(void)
{
    AssertValid(0);
    FNE_PAR::MarkMem();
    if (_prgftg != _rgftg)
        MarkPv(_prgftg);
    MarkMemObj(_pglfes);
}
#endif // 3DMMv1.0: DEBUG
