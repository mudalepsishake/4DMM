/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    bkgd.cpp: Background class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    A BKGD (background) consists of a set of light sources (GLLT), a
    background sound (SND), and one or more camera views.  Each camera view
    consists of a camera specification, a pre-rendered RGB bitmap, and a
    pre-rendered Z-buffer.  Here's how the chunks look:

    BKGD // Contains stage bounding cuboid
     |
     +---GLLT (chid 0) // GL of light position specs (LITEs)
     |
     +---SND  (chid 0) // Background sound/music
     |
     +---CAM  (chid 0) // Contains camera pos/orient matrix, hither, yon
     |    |
     |    +---MBMP (chid 0) // Background RGB bitmap
     |    |
     |    +---ZBMP (chid 0) // Background Z-buffer
     |
     +---CAM (chid 1)
     |    |
     |    +---MBMP (chid 0)
     |    |
     |    +---ZBMP (chid 0)
     |
     +---CAM (chid 2)
     .    |
     .    +---MBMP (chid 0)
     .    |
          +---ZBMP (chid 0)

***************************************************************************/
#include "soc.h"
ASSERTNAME

extern void DiagLogBRender(const char *pszFormat, ...);

RTCLASS(BKGD)

const CHID kchidBds = 0;  // 3DMMv1.0: Background default sound
const CHID kchidGllt = 0; // 3DMMv1.0: GL of LITEs
const CHID kchidGlcr = 0; // 3DMMv1.0: Palette
const br_colour kbrcLight = BR_COLOUR_RGB(0xff, 0xff, 0xff);

/** 3DMMv1.0: *************************************************************************
    Add the background's chunks (excluding camera views) to the tag list
***************************************************************************/
bool BKGD::FAddTagsToTagl(PTAG ptagBkgd, PTAGL ptagl)
{
    AssertVarMem(ptagBkgd);
    AssertPo(ptagl, 0);

    if (!ptagl->FInsertTag(ptagBkgd, fFalse))
        return fFalse;
    if (!ptagl->FInsertChild(ptagBkgd, kchidBds, kctgBds))
        return fFalse;
    if (!ptagl->FInsertChild(ptagBkgd, kchidGllt, kctgGllt))
        return fFalse;
    if (!ptagl->FInsertChild(ptagBkgd, kchidGlcr, kctgColorTable))
        return fFalse;

    // 3DMMv1.0: Have to cache first camera view since scene switches to it
    // 3DMMv1.0: automatically
    if (!ptagl->FInsertChild(ptagBkgd, 0, kctgCam))
        return fFalse;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Cache the background's chunks (excluding camera views) to HD
***************************************************************************/
bool BKGD::FCacheToHD(PTAG ptagBkgd)
{
    AssertVarMem(ptagBkgd);

    TAG tagBds;
    TAG tagGllt;
    TAG tagGlcr;
    TAG tagCam;

    // 3DMMv1.0: Build the child tags
    if (!vptagm->FBuildChildTag(ptagBkgd, kchidBds, kctgBds, &tagBds))
        return fFalse;
    if (!vptagm->FBuildChildTag(ptagBkgd, kchidGllt, kctgGllt, &tagGllt))
        return fFalse;
    if (!vptagm->FBuildChildTag(ptagBkgd, kchidGlcr, kctgColorTable, &tagGlcr))
        return fFalse;
    if (!vptagm->FBuildChildTag(ptagBkgd, 0, kctgCam, &tagCam))
        return fFalse;

    // 3DMMv1.0: Cache the BKGD chunk
    if (!vptagm->FCacheTagToHD(ptagBkgd, fFalse))
        return fFalse;

    // 3DMMv1.0: Cache the child chunks
    if (!vptagm->FCacheTagToHD(&tagBds))
        return fFalse;
    if (!vptagm->FCacheTagToHD(&tagGllt))
        return fFalse;
    if (!vptagm->FCacheTagToHD(&tagGlcr))
        return fFalse;

    // 3DMMv1.0: Have to cache first camera view since scene switches to it
    // 3DMMv1.0: automatically
    if (!vptagm->FCacheTagToHD(&tagCam))
        return fFalse;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    A PFNRPO to read a BKGD from a file
***************************************************************************/
bool BKGD::FReadBkgd(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    BKGD *pbkgd;

    *pcb = SIZEOF(BKGD) + SIZEOF(BACT) + SIZEOF(BLIT); // 3DMMv1.0: estimate BKGD size
    if (pvNil == ppbaco)
        return fTrue;
    pbkgd = NewObj BKGD;
    if (pvNil == pbkgd || !pbkgd->_FInit(pcrf->Pcfl(), ctg, cno))
    {
        TrashVar(ppbaco);
        TrashVar(pcb);
        ReleasePpo(&pbkgd);
        return fFalse;
    }
    AssertPo(pbkgd, 0);
    *ppbaco = pbkgd;
    *pcb = SIZEOF(BKGD) + LwMul(pbkgd->_cbactLight, SIZEOF(BACT)) +
           LwMul(pbkgd->_cbactLight, SIZEOF(BLIT)); // 3DMMv1.0: actual BKGD size
    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Deserialize BDS from on-disk format
***************************************************************************/
bool DeserializeBDS(int16_t bo, BDS *pbds)
{
    BDSF bdsf;

    CopyPb(pbds, &bdsf, SIZEOF(BDSF));
    if (bo != bdsf.bo)
        SwapBytesBom(&bdsf, kbomBds);

    Assert(kboCur == bdsf.bo, "bad BDS");

    pbds->vlm = bdsf.vlm;
    pbds->fLoop = bdsf.fLoop;
    DeserializeTagfToTag(&bdsf.tagSnd, &pbds->tagSnd);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read a BKGD from the given chunk of the given CFL.
    Note: Although we read the data for the lights here, we don't turn
    them on yet because we don't have a BWLD to add them to.  The lights
    are	turned on with the first FSetCamera() call.
***************************************************************************/
bool BKGD::_FInit(PCFL pcfl, CTG ctg, CNO cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);
#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::_FInit BEGIN this=%p ctg=0x%08lX cno=0x%08lX",
                this, (unsigned long)ctg, (unsigned long)cno);
    const char *pszModernStage = "begin";
#endif

    BLCK blck;
    BKGDF bkgdf;
    KID kid;
    PGL pgllite = pvNil;
    int16_t bo;
    int32_t cbactLightAuthored = 0;
    int32_t cbactLightExtra = 0;

    _ccam = _Ccam(pcfl, ctg, cno); // 3DMMv1.0: compute # of views in this background
#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::_FInit cameras=%ld", (long)_ccam);
    pszModernStage = "background chunk";
#endif
    _icam = ivNil;
    _fCamBaseValid = fFalse;

    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData())
        goto LFail;
    if (blck.Cb() != SIZEOF(BKGDF))
        goto LFail;
    if (!blck.FReadRgb(&bkgdf, SIZEOF(BKGDF), 0))
        goto LFail;
    if (kboCur != bkgdf.bo)
        SwapBytesBom(&bkgdf, kbomBkgdf);
    Assert(kboCur == bkgdf.bo, "bad BKGDF");

    if (!pcfl->FGetName(ctg, cno, &_stn))
        goto LFail;

    // 3DMMv1.0: Get the default sound
    if (pcfl->FGetKidChidCtg(ctg, cno, kchidBds, kctgBds, &kid))
    {
        if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck) || !blck.FUnpackData())
            goto LFail;
        if (blck.Cb() != SIZEOF(BDSF))
            goto LFail;
        if (!blck.FReadRgb(&_bds, SIZEOF(BDSF), 0))
            goto LFail;
        if (!DeserializeBDS(kboCur, &_bds))
            goto LFail;
    }
    else
    {
        _bds.tagSnd.sid = ksidInvalid;
    }

    // 3DMMv1.0: If there is a GLCR child, get it
    if (pcfl->FGetKidChidCtg(ctg, cno, kchidGlcr, kctgColorTable, &kid))
    {
        if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
            goto LFail;
        _pglclr = GL::PglRead(&blck, &bo);
        if (_pglclr != pvNil)
        {
            if (kboOther == bo)
                SwapBytesRglw(_pglclr->QvGet(0), _pglclr->IvMac());
        }
        _bIndexBase = bkgdf.bIndexBase;
    }
    else
        _pglclr = pvNil;

    // 3DMMv1.0: Read the GL of LITEs (GLLT)
#if defined(BRENDER_MODERN_14)
    pszModernStage = "light list";
#endif
    if (!pcfl->FGetKidChidCtg(ctg, cno, kchidGllt, kctgGllt, &kid))
        goto LFail;
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        goto LFail;
    pgllite = GL::PglRead(&blck, &bo);
    if (pvNil == pgllite)
        goto LFail;
    Assert(pgllite->CbEntry() == SIZEOF(LITE), "bad pgllite...you may need to update bkgds.chk");
    AssertBomRglw(kbomLite, SIZEOF(LITE));
    if (kboOther == bo)
    {
        SwapBytesRglw(pgllite->QvGet(0), LwMul(pgllite->IvMac(), SIZEOF(LITE) / SIZEOF(int32_t)));
    }
    // Keep all authored lights exactly as-is.  In true-colour mode, add one
    // gentle camera-side point light after them.  The original 3DMM stages
    // mostly use directional lighting, which is physically uniform across a
    // large flat face.  That is why a heavily flattened 3D Word (for example
    // an underscore used as a floor/wall panel) can collapse into one broad
    // shade even though its RGB888 material is smoothly lit.  A weak point
    // source gives BRender a real position-dependent light vector at every
    // vertex, so broad TDT surfaces gain depth while preserving the authored
    // stage lights and their overall look.
    cbactLightAuthored = pgllite->IvMac();
    // actorlight9: the extra tdtlight point source was introduced solely for
    // flattened 3D Text experiments.  Do not mix it into -a actor/prop
    // lighting, otherwise the street-light test has two unrelated positional
    // sources before we even begin.  Plain -c keeps the TDT fill exactly as
    // before.
    cbactLightExtra = (BWLD::FTrueColorMode() && !BWLD::FActorLightMode()) ? 1 : 0;
    _cbactLight = cbactLightAuthored;
    if (!FAllocPv((void **)&_prgbactLight, LwMul(_cbactLight + cbactLightExtra, SIZEOF(BACT)), fmemClear, mprNormal))
    {
        goto LFail;
    }
    if (!FAllocPv((void **)&_prgblitLight, LwMul(_cbactLight + cbactLightExtra, SIZEOF(BLIT)), fmemClear, mprNormal))
    {
        goto LFail;
    }
    _SetupLights(pgllite);

    if (cbactLightExtra != 0)
    {
        BACT *pbactFill = &_prgbactLight[_cbactLight];
        BLIT *pblitFill = &_prgblitLight[_cbactLight];

        // tdtlight6: tdtlight5 proved the tessellated glyph reaches the RGB888
        // Gouraud renderer, but every sampled vertex saturated at 254.  BRender
        // evaluates point-light attenuation as 1 / (c + l*d + q*d*d), so the
        // old c=0.75/l=0.00125 source was still effectively near full strength
        // around the stage and continued to contribute heavily even at distance.
        // Keep the positional source, but give the authored lighting headroom:
        // a half-white light plus a much softer linear attenuation curve.
        // tdtlight7: the flattened TDT is produced by an extreme non-uniform
        // actor scale.  BRender's own bounds renderer notes that lighting a
        // scaled model can behave incorrectly unless the light is evaluated
        // in view space.  Keep the same point source and attenuation from
        // tdtlight6, but evaluate it after the model/view transform so the
        // giant displayed floor samples the light across its actual stretched
        // extent rather than effectively lighting the tiny source glyph.
        pblitFill->type = BR_LIGHT_VIEW | BR_LIGHT_POINT;
        pblitFill->colour = BR_COLOUR_RGB(0x80, 0x80, 0x80);
        pblitFill->attenuation_c = BR_SCALAR(2.50);
        pblitFill->attenuation_l = BR_SCALAR(0.00500);
        pblitFill->attenuation_q = rZero;

        pbactFill->type = BR_ACTOR_LIGHT;
        pbactFill->type_data = pblitFill;
        pbactFill->t.type = BR_TRANSFORM_MATRIX34;
        BrMatrix34Identity(&pbactFill->t.t.mat);
        _cbactLight++;
    }
    ReleasePpo(&pgllite);
#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::_FInit SUCCESS this=%p cameras=%ld lights=%ld name_len=%ld",
                this, (long)_ccam, (long)_cbactLight, (long)_stn.Cch());
#endif
    return fTrue;
LFail:
#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::_FInit FAIL stage=%s ctg=0x%08lX cno=0x%08lX",
                pszModernStage, (unsigned long)ctg, (unsigned long)cno);
#endif
    Warn("Error reading background");
    ReleasePpo(&pgllite);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Return the number of camera views in this scene.  CAM chunks need to be
    contiguous CHIDs starting at CHID 0.
***************************************************************************/
int32_t BKGD::_Ccam(PCFL pcfl, CTG ctg, CNO cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    KID kid;
    int32_t ccam;

    for (ccam = 0; pcfl->FGetKidChidCtg(ctg, cno, ccam, kctgCam, &kid); ccam++)
    {
    }
#ifdef DEBUG
    // 3DMMv1.0: Make sure chids are consecutive
    int32_t ckid;
    int32_t ccamT = 0;
    for (ckid = 0; pcfl->FGetKid(ctg, cno, ckid, &kid); ckid++)
    {
        if (kid.cki.ctg == kctgCam)
            ccamT++;
    }
    Assert(ccamT == ccam, "cam chids are not consecutive!");
#endif
    return ccam;
}

/** 3DMMv1.0: *************************************************************************
    Fill _prgbactLight and _prgblitLight using a GL of LITEs
***************************************************************************/
void BKGD::_SetupLights(PGL pgllite)
{
    AssertBaseThis(0);
    AssertPo(pgllite, 0);

    int32_t ilite;
    LITE *qlite;
    BACT *pbact;
    BLIT *pblit;

    for (ilite = 0; ilite < _cbactLight; ilite++)
    {
        qlite = (LITE *)pgllite->QvGet(ilite);
        pbact = &_prgbactLight[ilite];
        pblit = &_prgblitLight[ilite];
        pblit->type = (uint8_t)qlite->lt;
        pblit->colour = kbrcLight;
        pblit->attenuation_c = qlite->rIntensity;
        pbact->type = BR_ACTOR_LIGHT;
        pbact->type_data = pblit;
        pbact->t.type = BR_TRANSFORM_MATRIX34;
        pbact->t.t.mat = qlite->bmat34;
    }
}

/** 3DMMv1.0: *************************************************************************
    Clean up and delete this background
***************************************************************************/
BKGD::~BKGD(void)
{
    AssertBaseThis(0);
    Assert(!_fLeaveLitesOn, "Shouldn't be freeing background now");
    if (pvNil != _prgbactLight && pvNil != _prgblitLight)
        TurnOffLights();
    FreePpv((void **)&_prgbactLight);
    FreePpv((void **)&_prgblitLight);
    ReleasePpo(&_pglclr);
    ReleasePpo(&_pglapos);
}

/** 3DMMv1.0: *************************************************************************
    Get the background's name
***************************************************************************/
void BKGD::GetName(PSTN pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    *pstn = _stn;
}

/** 3DMMv1.0: *************************************************************************
    Get the custom palette for this background, if any.  Returns fFalse if
    an error occurs.  Sets *ppglclr to an empty GL and *piclrMin to 0 if
    this background has no custom palette.
***************************************************************************/
bool BKGD::FGetPalette(PGL *ppglclr, int32_t *piclrMin)
{
    AssertThis(0);
    AssertVarMem(ppglclr);
    AssertVarMem(piclrMin);

    *piclrMin = _bIndexBase;
    if (pvNil == _pglclr) // 3DMMv1.0: no custom palette
    {
        *ppglclr = GL::PglNew(SIZEOF(CLR)); // 3DMMv1.0: "palette" with 0 entries
    }
    else
    {
        *ppglclr = _pglclr->PglDup();
    }
    return (pvNil != *ppglclr);
}

/** 3DMMv1.0: *************************************************************************
    Get the camera position in worldspace
***************************************************************************/
void BKGD::GetCameraPos(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertNilOrVarMem(pxr);
    AssertNilOrVarMem(pyr);
    AssertNilOrVarMem(pzr);

    if (pvNil != pxr)
        *pxr = _xrCam;
    if (pvNil != pyr)
        *pyr = _yrCam;
    if (pvNil != pzr)
        *pzr = _zrCam;
}

/***************************************************************************
    Return the exact camera state last selected by vanilla 3DMM.
***************************************************************************/
bool BKGD::FGetCameraBase(BMAT34 *pbmat34, BRS *pzrHither, BRS *pzrYon, BRA *paFov)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    AssertVarMem(pzrHither);
    AssertVarMem(pzrYon);
    AssertVarMem(paFov);

    if (!_fCamBaseValid)
        return fFalse;

    *pbmat34 = _bmat34CamBase;
    *pzrHither = _zrHitherCamBase;
    *pzrYon = _zrYonCamBase;
    *paFov = _aFovCamBase;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Turn on lights in pbwld
***************************************************************************/
void BKGD::TurnOnLights(PBWLD pbwld)
{
    AssertThis(0);
    AssertPo(pbwld, 0);

    int32_t ilite;
    BACT *pbact;

    if (!_fLites)
    {
        // Light Lab replaces the scene's authored/default lights only while
        // dynamic Light Lab lighting is actually ON.  When CTRL+SHIFT+L turns
        // Light Lab off, 3DMM's authored background lights are the default
        // lighting rig and must remain active so legacy 3D Words/props keep
        // their normal shaded appearance.
        const bool fStreetLightLab =
            BWLD::FTrueColorMode() && BWLD::FActorLightMode() && MVIE::FSceneDynamicLightingActive() &&
            !MVIE::FSceneLightLabCombineLegacyActive();

        if (!fStreetLightLab)
        {
            for (ilite = 0; ilite < _cbactLight; ilite++)
            {
                pbact = &_prgbactLight[ilite];
                pbwld->AddActor(pbact);
                BrLightEnable(pbact);
            }
        }
        else
        {
            DiagLogBRender("actorlight10 STREETLIGHT lab: suppressed %d authored background light(s)",
                           (int)_cbactLight);
        }

        _fLites = fTrue;
    }
}

/** 3DMMv1.0: *************************************************************************
    Turn off lights in pbwld
***************************************************************************/
void BKGD::TurnOffLights(void)
{
    AssertThis(0);

    int32_t ilite;
    BACT *pbact;

    if (!_fLites || _fLeaveLitesOn)
        return;

    // Keep removal perfectly symmetrical with TurnOnLights().  Dynamic Light
    // Lab suppresses authored lights, so there is nothing to remove in that
    // state.  Default-lighting mode attaches them normally and removes them
    // normally during scene/toggle transitions.
    const bool fStreetLightLab =
        BWLD::FTrueColorMode() && BWLD::FActorLightMode() && MVIE::FSceneDynamicLightingActive() &&
        !MVIE::FSceneLightLabCombineLegacyActive();
#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::TurnOffLights this=%p lights=%ld leave=%d light_lab=%d dynamic=%d",
                this, (long)_cbactLight, (int)_fLeaveLitesOn, (int)fStreetLightLab,
                (int)MVIE::FSceneDynamicLightingActive());
#endif
    if (!fStreetLightLab)
    {
        for (ilite = 0; ilite < _cbactLight; ilite++)
        {
            pbact = &_prgbactLight[ilite];
#if defined(BRENDER_MODERN_14)
            BrModernLog("BKGD::TurnOffLights removing authored light i=%ld actor=%p parent=%p prev=%p",
                        (long)ilite, pbact, pbact->parent, pbact->prev);
#endif
            BrLightDisable(pbact);
            BrActorRemove(pbact);
        }
    }
#if defined(BRENDER_MODERN_14)
    else
    {
        BrModernLog("BKGD::TurnOffLights skip authored removal: dynamic Light Lab suppressed these actors at TurnOnLights");
    }
#endif
    _fLites = fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Set the camera and associated bitmaps to icam
***************************************************************************/
bool BKGD::FSetCamera(PBWLD pbwld, int32_t icam)
{
    AssertThis(0);
    AssertPo(pbwld, 0);
    AssertIn(icam, 0, Ccam());
#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::FSetCamera BEGIN this=%p bwld=%p ctg=0x%08lX cno=0x%08lX icam=%ld/%ld",
                this, pbwld, (unsigned long)Ctg(), (unsigned long)Cno(), (long)icam, (long)Ccam());
#endif

    int32_t capos;
    KID kidCam;
    KID kidRGB;
    KID kidZ;
    BLCK blck;
    CAM cam;
    PCFL pcfl = Pcrf()->Pcfl();
    BREUL breul;

    TurnOnLights(pbwld);

    // 3DMMv1.0: read new camera data
    if (!pcfl->FGetKidChidCtg(Ctg(), Cno(), icam, kctgCam, &kidCam))
    {
#if defined(BRENDER_MODERN_14)
        BrModernLog("BKGD::FSetCamera FAIL camera child lookup");
#endif
        return fFalse;
    }
#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::FSetCamera camera kid ctg=0x%08lX cno=0x%08lX",
                (unsigned long)kidCam.cki.ctg, (unsigned long)kidCam.cki.cno);
#endif
    if (!pcfl->FFind(kidCam.cki.ctg, kidCam.cki.cno, &blck) || !blck.FUnpackData())
    {
#if defined(BRENDER_MODERN_14)
        BrModernLog("BKGD::FSetCamera FAIL camera chunk read/unpack");
#endif
        return fFalse;
    }

    // 3DMMv1.0: Need at least one actor position
    if (blck.Cb() < SIZEOF(CAM))
    {
        Bug("CAM chunk not large enough");
        return fFalse;
    }
    capos = (blck.Cb() - SIZEOF(CAM)) / SIZEOF(APOS);
    if ((capos * SIZEOF(APOS) + SIZEOF(CAM)) != blck.Cb())
    {
        Bug("CAM chunk's extra data not an even multiple of SIZEOF(APOS)");
        return fFalse;
    }

    if (!blck.FReadRgb(&cam, SIZEOF(CAM), 0))
        return fFalse;

#ifdef DEBUG
    {
        BOM bomCam = kbomCam, bomCamOld = kbomCamOld;
        Assert(bomCam == bomCamOld, "BOM macros aren't right");
    }
#endif // 3DMMv1.0: DEBUG

    Assert((SIZEOF(APOS) / SIZEOF(int32_t)) * SIZEOF(int32_t) == SIZEOF(APOS), "APOS not an even number of longs");
    if (kboOther == cam.bo)
    {
        SwapBytesBom(&cam, kbomCam);
        SwapBytesRglw(PvAddBv(&cam, offset(CAM, bmat34Cam)), SIZEOF(cam.bmat34Cam) / SIZEOF(int32_t));
        SwapBytesRglw(PvAddBv(&cam, SIZEOF(CAM)), capos * (SIZEOF(APOS) / SIZEOF(int32_t)));
    }
    Assert(kboCur == cam.bo, "bad cam");

    // 3DMMv1.0: find RGB pict
    if (!pcfl->FGetKidChidCtg(kidCam.cki.ctg, kidCam.cki.cno, 0, kctgMbmp, &kidRGB))
    {
#if defined(BRENDER_MODERN_14)
        BrModernLog("BKGD::FSetCamera FAIL RGB MBMP child lookup");
#endif
        return fFalse;
    }
    // 3DMMv1.0: find Z pict
    if (!pcfl->FGetKidChidCtg(kidCam.cki.ctg, kidCam.cki.cno, 0, kctgZbmp, &kidZ))
    {
#if defined(BRENDER_MODERN_14)
        BrModernLog("BKGD::FSetCamera FAIL ZBMP child lookup");
#endif
        return fFalse;
    }
#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::FSetCamera children RGB=(0x%08lX,0x%08lX) Z=(0x%08lX,0x%08lX) capos=%ld",
                (unsigned long)kidRGB.cki.ctg, (unsigned long)kidRGB.cki.cno,
                (unsigned long)kidZ.cki.ctg, (unsigned long)kidZ.cki.cno, (long)capos);
#endif

    // The authored background MBMP is still 8-bit indexed even when -c is
    // rendering the BRender world into RGB888.  GPT::DrawMbmp expands that
    // indexed bitmap through the currently active Kauai palette.  The old
    // transition path normally installed this background's custom palette,
    // but true-colour transitions deliberately bypass that legacy path.
    // Install only the palette metadata here, before FSetBackground converts
    // the MBMP into the RGB888 background surface.
    if (BWLD::FTrueColorMode())
    {
        PGL pglclrSystem = GPT::PglclrGetPalette();
        PGL pglclrBkgd = pvNil;
        int32_t iclrMin = 0;

        if (pvNil != pglclrSystem && FGetPalette(&pglclrBkgd, &iclrMin) && pvNil != pglclrBkgd)
        {
            Assert(pglclrBkgd->IvMac() + iclrMin <= pglclrSystem->IvMac(),
                   "Background palette too large");
            if (pglclrBkgd->IvMac() + iclrMin <= pglclrSystem->IvMac())
            {
                CopyPb(pglclrBkgd->QvGet(0), pglclrSystem->QvGet(iclrMin),
                       LwMul(SIZEOF(CLR), pglclrBkgd->IvMac()));
                GPT::SetActiveColors(pglclrSystem, fpalIdentity);
            }
        }

        ReleasePpo(&pglclrBkgd);
        ReleasePpo(&pglclrSystem);
    }

#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::FSetCamera BWLD::FSetBackground BEGIN");
#endif
    if (!pbwld->FSetBackground(Pcrf(), kidRGB.cki.ctg, kidRGB.cki.cno, kidZ.cki.ctg, kidZ.cki.cno))
    {
#if defined(BRENDER_MODERN_14)
        BrModernLog("BKGD::FSetCamera FAIL BWLD::FSetBackground");
#endif
        return fFalse;
    }
#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::FSetCamera BWLD::FSetBackground returned SUCCESS");
#endif

    // 3DMMv1.0: Get actor placements
    ReleasePpo(&_pglapos);
    _iaposNext = _iaposLast = 0;
    if (capos > 0 && (_pglapos = GL::PglNew(SIZEOF(APOS), capos)) != pvNil)
    {
        AssertDo(_pglapos->FSetIvMac(capos), "Should never fail");
        _pglapos->Lock();
        if (!blck.FReadRgb(_pglapos->QvGet(0), SIZEOF(APOS) * capos, SIZEOF(CAM)))
        {
            ReleasePpo(&_pglapos);
            return fFalse;
        }
        _pglapos->Unlock();
    }

    _icam = icam;
    _xrPlace = cam.apos.xrPlace;
    _yrPlace = cam.apos.yrPlace;
    _zrPlace = cam.apos.zrPlace;

    _xrCam = cam.bmat34Cam.m[3][0];
    _yrCam = cam.bmat34Cam.m[3][1];
    _zrCam = cam.bmat34Cam.m[3][2];

    _bmat34CamBase = cam.bmat34Cam;
    _zrHitherCamBase = cam.zrHither;
    _zrYonCamBase = cam.zrYon;
    _aFovCamBase = cam.aFov;
    _fCamBaseValid = fTrue;

    // The final light is the true-colour dimensional fill added in _FInit.
    // tdtlight6: keep the fill close to the object/stage region, but no longer overdrive it.
    // Start at the authored actor placement point, move 18% of the way back
    // toward the authored camera (so the light is on the visible side of a
    // typical object), then bias it toward camera-left and camera-up.  Keeping
    // this light physically close to the geometry produces meaningfully
    // different light vectors across a wide flattened 3D Word, which Gouraud
    // shading can then interpolate into a real surface gradient.
    //
    // The light remains fixed in world space after this setup.  Manual Camera
    // and Free Look therefore move relative to the light rather than dragging
    // it around as a viewer-attached headlamp.
    if (BWLD::FTrueColorMode() && _cbactLight > 0)
    {
        const int32_t iliteFill = _cbactLight - 1;
        BLIT *pblitFill = &_prgblitLight[iliteFill];
        BACT *pbactFill = &_prgbactLight[iliteFill];
        if ((pblitFill->type & BR_LIGHT_TYPE) == BR_LIGHT_POINT)
        {
            const BRS rTowardCamera = BR_SCALAR(0.18);
            BMAT34 bmat34CamRotation = cam.bmat34Cam;
            BVEC3 bvec3Offset;
            BVEC3 bvec3WorldOffset;

            // Remove translation so BrMatrix34ApplyP is used only to rotate
            // our screen-left/up offset into the authored camera's world axes.
            bmat34CamRotation.m[3][0] = rZero;
            bmat34CamRotation.m[3][1] = rZero;
            bmat34CamRotation.m[3][2] = rZero;
            bvec3Offset.v[0] = BR_SCALAR(-140.0);
            bvec3Offset.v[1] = BR_SCALAR(220.0);
            bvec3Offset.v[2] = rZero;
            BrMatrix34ApplyP(&bvec3WorldOffset, &bvec3Offset, &bmat34CamRotation);

            BRS xrLight = cam.apos.xrPlace +
                BrsMul(cam.bmat34Cam.m[3][0] - cam.apos.xrPlace, rTowardCamera) + bvec3WorldOffset.v[0];
            BRS yrLight = cam.apos.yrPlace +
                BrsMul(cam.bmat34Cam.m[3][1] - cam.apos.yrPlace, rTowardCamera) + bvec3WorldOffset.v[1];
            BRS zrLight = cam.apos.zrPlace +
                BrsMul(cam.bmat34Cam.m[3][2] - cam.apos.zrPlace, rTowardCamera) + bvec3WorldOffset.v[2];

            BrMatrix34Identity(&pbactFill->t.t.mat);
            BrMatrix34PostTranslate(&pbactFill->t.t.mat, xrLight, yrLight, zrLight);
            DiagLogBRender("tdtlight7 VIEW-space point light pos=(%.2f,%.2f,%.2f) colour=(128,128,128) attenuation=(c=2.50,l=0.00500,q=0)",
                           (double)BrScalarToFloat(xrLight),
                           (double)BrScalarToFloat(yrLight),
                           (double)BrScalarToFloat(zrLight));
        }
    }

    // 3DMMv1.0: Find bmat34 without X & Z rotation
    breul.order = BR_EULER_YXY_R;
    BrMatrix34ToEuler(&breul, &cam.bmat34Cam);
    _braRotY = breul.a + breul.c;
    BrMatrix34RotateY(&_bmat34Mouse, _braRotY);
    BrMatrix34PostTranslate(&_bmat34Mouse, cam.bmat34Cam.m[3][0], cam.bmat34Cam.m[3][1], cam.bmat34Cam.m[3][2]);

    pbwld->SetCamera(&cam.bmat34Cam, cam.zrHither, cam.zrYon, cam.aFov);
    pbwld->MarkDirty();
#if defined(BRENDER_MODERN_14)
    BrModernLog("BKGD::FSetCamera SUCCESS icam=%ld hither=%.6f yon=%.6f fov=%.6f",
                (long)icam, (double)BrScalarToFloat(cam.zrHither),
                (double)BrScalarToFloat(cam.zrYon), (double)BrScalarToFloat(BrAngleToDegree(cam.aFov)));
#endif
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Gets the matrix for mouse-dragging relative to the camera
***************************************************************************/
void BKGD::GetMouseMatrix(BMAT34 *pbmat34)
{
    AssertThis(0);
    AssertVarMem(pbmat34);

    *pbmat34 = _bmat34Mouse;
}

/** 3DMMv1.0: *************************************************************************
    Gets the point at which to place new actors for this bkgd/view.
***************************************************************************/
void BKGD::GetActorPlacePoint(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);

    APOS apos;

    if (_iaposNext == 0)
    {
        *pxr = _xrPlace;
        *pyr = _yrPlace;
        *pzr = _zrPlace;
    }
    else
    {
        _pglapos->Get(_iaposNext - 1, &apos);
        *pxr = apos.xrPlace;
        *pyr = apos.yrPlace;
        *pzr = apos.zrPlace;
    }

    _iaposLast = _iaposNext;
    if (_pglapos != pvNil)
    {
        _iaposNext++;
        if (_iaposNext > _pglapos->IvMac())
            _iaposNext = 0;
    }
}

/***************************************************************************
    Gets the authored/base actor placement point without advancing the
    background's placement-point cycle.  Experimental scene lights use this
    as a stable world-space anchor.
***************************************************************************/
void BKGD::GetDefaultActorPlacePoint(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);

    *pxr = _xrPlace;
    *pyr = _yrPlace;
    *pzr = _zrPlace;
}

/** 3DMMv1.0: ****************************************************************************
    ReuseActorPlacePoint
        Resets the current actor place point to the last one used.  Call this
        from the actor placement code if the actor was placed at a point other
        than the one you just asked for.
************************************************************ PETED ***********/
void BKGD::ReuseActorPlacePoint(void)
{
    _iaposNext = _iaposLast;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Authoring only.  Writes a special file with the given place info.
***************************************************************************/
bool BKGD::FWritePlaceFile(BRS xrPlace, BRS yrPlace, BRS zrPlace)
{
    AssertThis(0);
    Assert(yrPlace == rZero, "are you sure you want non-zero Y?");

    STN stnFile;
    FNI fni;
    PFIL pfil = pvNil;
    STN stnData;
    FP fp;
    int32_t xr1 = BrScalarToInt(xrPlace);
    int32_t xr2 = LwAbs((int32_t)(1000000.0 * BrScalarToFloat(xrPlace - BrIntToScalar(xr1))));
    int32_t yr1 = BrScalarToInt(yrPlace);
    int32_t yr2 = LwAbs((int32_t)(1000000.0 * BrScalarToFloat(yrPlace - BrIntToScalar(yr1))));
    int32_t zr1 = BrScalarToInt(zrPlace);
    int32_t zr2 = LwAbs((int32_t)(1000000.0 * BrScalarToFloat(zrPlace - BrIntToScalar(zr1))));

    if (!stnFile.FFormatSz(PszLit("%s-cam.1-%d.pos"), &_stn, _icam + 1))
        goto LFail;
    if (!fni.FBuildFromPath(&stnFile))
        goto LFail;
    if (fni.TExists() == tYes)
        pfil = FIL::PfilOpen(&fni, ffilWriteEnable);
    else
        pfil = FIL::PfilCreate(&fni);
    if (pvNil == pfil)
        goto LFail;
    if (!stnData.FFormatSz(PszLit("NEW_ACTOR_POS %d.%06d %d.%06d %d.%06d\n\r"), xr1, xr2, yr1, yr2, zr1, zr2))
    {
        goto LFail;
    }
    if ((fp = pfil->FpMac()) > 0)
    {
        // 3DMMv1.0: Go to the end of the file (and write over null byte at end
        // 3DMMv1.0: of previous string)
        fp--;
    }
    if (!pfil->FWriteRgb(stnData.Psz(), stnData.Cch() + 1, fp))
        goto LFail;
    ReleasePpo(&pfil);
    return fTrue;
LFail:
    ReleasePpo(&pfil);
    return fFalse;
}
#endif // 3DMMv1.0: DEBUG

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the BKGD.
***************************************************************************/
void BKGD::AssertValid(uint32_t grf)
{
    BKGD_PAR::AssertValid(fobjAllocated);
    AssertIn(_cbactLight, 1, 100); // 3DMMv1.0: 100 is sanity check
    AssertIn(_ccam, 1, 100);       // 3DMMv1.0: 100 is sanity check
    Assert(_icam == ivNil || (_icam >= 0 && _icam < _ccam), "bad _icam");
    AssertPvCb(_prgbactLight, LwMul(_cbactLight, SIZEOF(BACT)));
    AssertPvCb(_prgblitLight, LwMul(_cbactLight, SIZEOF(BLIT)));
    AssertPo(&_stn, 0);
    AssertNilOrPo(_pglapos, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the BKGD
***************************************************************************/
void BKGD::MarkMem(void)
{
    AssertThis(0);
    BKGD_PAR::MarkMem();
    MarkPv(_prgbactLight);
    MarkPv(_prgblitLight);
    MarkMemObj(_pglclr);
    MarkMemObj(_pglapos);
}
#endif // 3DMMv1.0: DEBUG
