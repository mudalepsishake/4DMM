/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    mtrl.cpp: Material (MTRL) and custom material (CMTL) classes

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "soc.h"
#if defined(BRENDER_MODERN_14)
#include "fmt.h"
#include <limits.h>
#endif
ASSERTNAME

RTCLASS(MTRL)
RTCLASS(CMTL)

// 3DMMv1.0: REVIEW *****: kiclrBaseDefault and kcclrDefault are palette-specific
const uint8_t kiclrBaseDefault = 15; // 3DMMv1.0: base index of default color
const uint8_t kcclrDefault = 15;     // 3DMMv1.0: count of shades in default color

const br_ufraction kbrufKaDefault = BR_UFRACTION(0.10);
const br_ufraction kbrufKdDefault = BR_UFRACTION(0.60);
const br_ufraction kbrufKsDefault = BR_UFRACTION(0.60);
const BRS krPowerDefault = BR_SCALAR(50);
const uint8_t kbOpaque = 0xff;

PTMAP MTRL::_ptmapShadeTable = pvNil; // 3DMMv1.0: shade table for all MTRLs

/***************************************************************************
    Modern BRender 1.4 does not implicitly create stored texture objects when
    a material merely points at a pixelmap. The old 3DMM code path assumed the
    colour_map/index_shade children could stay as raw br_pixelmap pointers and
    that BrMaterialAdd() would be enough. In modern v1db, the material's stored
    state ends up capturing NULL texture handles unless the pixelmaps have gone
    through BrMapAdd()/BrMapUpdate() first, which produces the default-grey
    fallback that Kyle observed once geometry finally started rendering.
***************************************************************************/
static void EnsureModernMaterialPixelmapsStored(PBMTL pbmtl)
{
    AssertVarMem(pbmtl);
#if defined(BRENDER_MODERN_14)
    if (pbmtl->colour_map != pvNil && pbmtl->colour_map->stored == pvNil)
    {
        BrModernLog("MTRL map prep colour_map add BEGIN map=%p type=%u pixels=%p row=%ld wh=%ux%u flags=0x%04X stored=%p",
                    pbmtl->colour_map, (unsigned)pbmtl->colour_map->type,
                    pbmtl->colour_map->pixels, (long)pbmtl->colour_map->row_bytes,
                    (unsigned)pbmtl->colour_map->width, (unsigned)pbmtl->colour_map->height,
                    (unsigned)pbmtl->colour_map->flags, pbmtl->colour_map->stored);
        BrMapAdd(pbmtl->colour_map);
        BrModernLog("MTRL map prep colour_map add END map=%p stored=%p",
                    pbmtl->colour_map, pbmtl->colour_map->stored);
    }

    if (pbmtl->index_shade != pvNil && pbmtl->index_shade->stored == pvNil)
    {
        BrModernLog("MTRL map prep index_shade add BEGIN map=%p type=%u pixels=%p row=%ld wh=%ux%u flags=0x%04X stored=%p",
                    pbmtl->index_shade, (unsigned)pbmtl->index_shade->type,
                    pbmtl->index_shade->pixels, (long)pbmtl->index_shade->row_bytes,
                    (unsigned)pbmtl->index_shade->width, (unsigned)pbmtl->index_shade->height,
                    (unsigned)pbmtl->index_shade->flags, pbmtl->index_shade->stored);
        BrMapAdd(pbmtl->index_shade);
        BrModernLog("MTRL map prep index_shade add END map=%p stored=%p",
                    pbmtl->index_shade, pbmtl->index_shade->stored);
    }
#else
    (void)pbmtl;
#endif
}


/***************************************************************************
    Give an indexed solid material a real RGB base colour for -c rendering.
    Socrates stored zero in MTRLF::brc because the original indexed renderer
    ignored br_material::colour and used index_base/index_range instead.
***************************************************************************/
static void SetTrueColorMaterialColour(PBMTL pbmtl, bool fPreserveStoredColour = fFalse)
{
    AssertVarMem(pbmtl);

    if (!BWLD::FTrueColorMode())
        return;

    // Stock Socrates MTRL records stored brc == 0 because the indexed
    // renderer ignored br_material::colour.  Movie-owned materials created
    // by the Windows colour picker store an explicit RGB value in brc.
    // Preserve that value instead of replacing it with the palette fallback.
    if (!fPreserveStoredColour || pbmtl->colour == 0)
    {
        const int32_t iclr = pbmtl->index_base + pbmtl->index_range;
        PGL pglclr = GPT::PglclrGetPalette();
        CLR clr;
        if (pglclr != pvNil && FIn(iclr, 0, pglclr->IvMac()))
        {
            pglclr->Get(iclr, &clr);
            pbmtl->colour = BR_COLOUR_RGB(clr.bRed, clr.bGreen, clr.bBlue);
        }
        else
        {
            pbmtl->colour = BR_COLOUR_RGB(0xff, 0xff, 0xff);
        }
        ReleasePpo(&pglclr);
    }

    // The indexed renderer's shade ramps reach their authored bright colour
    // more readily than literal RGB lighting with the old ka/kd values.
    // Normalize only -c materials so white stays white and the original hue
    // families do not all collapse toward grey/brown.
    // Keep the brighter v6 response while RGB888 removes the 5-bit/channel
    // quantisation that was still producing visible contour bands.
    pbmtl->ka = BR_UFRACTION(0.28);
    pbmtl->kd = BR_UFRACTION(1.00);
    pbmtl->ks = BR_UFRACTION(0.05);
}

/***************************************************************************
    Read a 4DMM VXP2 truecolor texture.  T24M is a tiny metadata resource;
    after import its T24D children contain consecutive slices of the original
    encoded PNG.  Keeping the PNG compressed in the movie avoids the legacy
    TMAP 8-bit palette and 24-bit Chunky payload-size limits.  The PNG is only
    expanded here, directly into BRender's RGBA8888 runtime representation.
***************************************************************************/
static PTMAP PtmapRead4DMMTrueColor(PCFL pcfl, CNO cnoT24m)
{
#if !defined(BRENDER_MODERN_14)
    (void)pcfl;
    (void)cnoT24m;
    return pvNil;
#else
    if (pcfl == pvNil || cnoT24m == cnoNil)
        return pvNil;

    int32_t cbPng = 0;
    int32_t cpart = 0;
    KID kid;
    BLCK blck;
    for (CHID chid = 0; pcfl->FGetKidChidCtg(kctgT24M, cnoT24m, chid, kctgT24D, &kid); ++chid)
    {
        if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck) || !blck.FUnpackData())
            return pvNil;
        const int32_t cbPart = blck.Cb();
        if (cbPart <= 0 || cbPng > INT_MAX - cbPart)
            return pvNil;
        cbPng += cbPart;
        ++cpart;
    }
    if (cpart <= 0 || cbPng <= 0)
    {
        BrModernLog("VXP2 T24M decode FAIL cno=%ld reason=no_png_parts", (long)cnoT24m);
        return pvNil;
    }

    uint8_t *prgbPng = pvNil;
    if (!FAllocPv((void **)&prgbPng, cbPng, fmemNil, mprNormal))
        return pvNil;

    int32_t ib = 0;
    for (CHID chid = 0; chid < cpart; ++chid)
    {
        if (!pcfl->FGetKidChidCtg(kctgT24M, cnoT24m, chid, kctgT24D, &kid) ||
            !pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck) || !blck.FUnpackData())
        {
            FreePpv((void **)&prgbPng);
            return pvNil;
        }
        const int32_t cbPart = blck.Cb();
        if (!blck.FReadRgb(prgbPng + ib, cbPart, 0))
        {
            FreePpv((void **)&prgbPng);
            return pvNil;
        }
        ib += cbPart;
    }

    BPMP *pbpmp = BrFmtPNGLoadMemory(prgbPng, (br_size_t)cbPng);
    FreePpv((void **)&prgbPng);
    if (pbpmp == pvNil)
    {
        BrModernLog("VXP2 T24M decode FAIL cno=%ld encoded_bytes=%ld reason=png_decode",
                    (long)cnoT24m, (long)cbPng);
        return pvNil;
    }

    PTMAP ptmap = TMAP::PtmapNewFromBpmp(pbpmp);
    if (ptmap == pvNil)
    {
        BrPixelmapFree(pbpmp);
        return pvNil;
    }

    BrModernLog("VXP2 T24M decode OK cno=%ld encoded_bytes=%ld parts=%ld map=%p type=%u wh=%ux%u",
                (long)cnoT24m, (long)cbPng, (long)cpart, ptmap->Pbpmp(),
                (unsigned)ptmap->Pbpmp()->type, (unsigned)ptmap->Pbpmp()->width,
                (unsigned)ptmap->Pbpmp()->height);
    // Match PmtrlNewFromPix's established ownership transfer: the TMAP now
    // owns the copied br_pixelmap state and its identifier points back to it.
    return ptmap;
#endif
}

/***************************************************************************
    The bundled 1995 renderer has RGB888 Gouraud solids and RGB888 texture
    mapping, but not the later lit INDEX_8-texture/RGB888-shade primitive.
    In -c, mapped materials therefore use their palette-expanded RGB888 map
    directly and skip the unavailable indexed lighting stage.
***************************************************************************/
static void SetTrueColorMappedMaterial(PBMTL pbmtl)
{
    AssertVarMem(pbmtl);

    if (!BWLD::FTrueColorMode())
        return;

#if defined(BRENDER_MODERN_14)
    // Use linear texture filtering and smoothly interpolate between mip levels.
    pbmtl->flags |= BR_MATF_MAP_INTERPOLATION | BR_MATF_MIP_INTERPOLATION;
#endif

    pbmtl->colour = BR_COLOUR_RGB(0xff, 0xff, 0xff);
    pbmtl->index_shade = pvNil;
    pbmtl->index_base = 0;
    pbmtl->index_range = 0;

#ifdef BRENDER_ORIGINAL
    // The original precompiled 1995 renderer has an unlit RGB888 texture
    // primitive, but no RGB888 textured + interpolated-intensity primitive.
    // Keep the established unlit path there.
    pbmtl->flags &= ~(BR_MATF_LIGHT | BR_MATF_SMOOTH | BR_MATF_DITHER);
#else
    // actorlight4: actor/prop textured lighting is now explicitly opt-in.
    // Without -a, keep the established unlit RGB888 texture presentation so
    // old movies retain their normal 3DMM appearance.  With -a, use source
    // BRender's repaired RGB888 lit-texture primitive.
    if (!BWLD::FActorLightMode() || MVIE::FSceneFlatLightingActive() ||
        (!MVIE::FSceneDynamicLightingActive() && !MVIE::FSceneDefaultLightingShadersActive()))
    {
        pbmtl->flags &= ~(BR_MATF_LIGHT | BR_MATF_DITHER);
        pbmtl->ka = BR_UFRACTION(1.00);
        pbmtl->kd = BR_UFRACTION(0.00);
        pbmtl->ks = BR_UFRACTION(0.00);
    }
    else
    {
        pbmtl->flags |= BR_MATF_LIGHT | BR_MATF_SMOOTH;
        pbmtl->flags &= ~BR_MATF_DITHER;

        if (MVIE::FSceneDynamicLightingActive())
        {
            if (MVIE::FSceneLightLabCombineLegacyActive())
            {
                pbmtl->ka = BR_UFRACTION(0.35);
                pbmtl->kd = BR_UFRACTION(0.60);
            }
            else
            {
                // Preserve the v8 isolated-Light-Lab material response.
                pbmtl->ka = BR_UFRACTION(0.00);
                pbmtl->kd = BR_UFRACTION(1.00);
            }
        }
        else
        {
            pbmtl->ka = BR_UFRACTION(0.35);
            pbmtl->kd = BR_UFRACTION(0.60);
        }
        pbmtl->ks = BR_UFRACTION(0.00);
    }
#endif
}

/** 3DMMv1.0: *************************************************************************
    Call this function to assign the global shade table.  It is read from
    the given chunk.
***************************************************************************/
bool MTRL::FSetShadeTable(PCFL pcfl, CTG ctg, CNO cno)
{
    AssertPo(pcfl, 0);

    ReleasePpo(&_ptmapShadeTable);
    _ptmapShadeTable = TMAP::PtmapRead(pcfl, ctg, cno, fTrue);
    if (pvNil == _ptmapShadeTable)
        return fFalse;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Create a new solid-color material
***************************************************************************/
PMTRL MTRL::PmtrlNew(int32_t iclrBase, int32_t cclr)
{
    if (ivNil != iclrBase)
        AssertIn(iclrBase, 0, kbMax);
    if (ivNil != cclr)
        AssertIn(cclr, 0, kbMax - iclrBase);

    PMTRL pmtrl;

    pmtrl = NewObj MTRL;
    if (pvNil == pmtrl)
        return pvNil;

    // 3DMMEx: An arbitrary 8-character string is passed to BrMaterialAllocate (to
    // 3DMMv1.0: be stored in a string pointed to by _pbmtl->identifier).  The
    // 3DMMv1.0: contents of the string are then replaced by the "this" pointer.
    SZS szsMaterialName = "12345678";
    pmtrl->_pbmtl = BrMaterialAllocate((char *)szsMaterialName);
    if (pvNil == pmtrl->_pbmtl)
    {
        ReleasePpo(&pmtrl);
        return pvNil;
    }
    CopyPb(&pmtrl, pmtrl->_pbmtl->identifier, SIZEOF(PMTRL));

    pmtrl->_pbmtl->ka = kbrufKaDefault;
    pmtrl->_pbmtl->kd = kbrufKdDefault;
    pmtrl->_pbmtl->ks = kbrufKsDefault;
    pmtrl->_pbmtl->power = krPowerDefault;
    if (ivNil == iclrBase)
        pmtrl->_pbmtl->index_base = kiclrBaseDefault;
    else
        pmtrl->_pbmtl->index_base = (uint8_t)iclrBase;
    if (ivNil == cclr)
        pmtrl->_pbmtl->index_range = kcclrDefault;
    else
        pmtrl->_pbmtl->index_range = (uint8_t)cclr;

    SetTrueColorMaterialColour(pmtrl->_pbmtl);

    pmtrl->_pbmtl->opacity = kbOpaque; // 3DMMv1.0: all socrates objects are opaque
    pmtrl->_pbmtl->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD;
#if defined(BRENDER_MODERN_14)
    if (MVIE::FSceneFlatLightingActive())
        pmtrl->_pbmtl->flags &= ~BR_MATF_LIGHT;
#endif
    BrMaterialAdd(pmtrl->_pbmtl);
    AssertPo(pmtrl, 0);
    return pmtrl;
}

/** 3DMMv1.0: *************************************************************************
    A PFNRPO to read MTRL objects.
***************************************************************************/
bool MTRL::FReadMtrl(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    PMTRL pmtrl;

    *pcb = SIZEOF(MTRL);
    if (pvNil == ppbaco)
        return fTrue;
    pmtrl = NewObj MTRL;
    if (pvNil == pmtrl || !pmtrl->_FInit(pcrf, ctg, cno))
    {
        TrashVar(ppbaco);
        TrashVar(pcb);
        ReleasePpo(&pmtrl);
        return fFalse;
    }
    AssertPo(pmtrl, 0);
    *ppbaco = pmtrl;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read the given MTRL chunk from file
***************************************************************************/
bool MTRL::_FInit(PCRF pcrf, CTG ctg, CNO cno)
{
    AssertBaseThis(0);
    AssertPo(pcrf, 0);
#if defined(BRENDER_MODERN_14)
    BrModernLog("MTRL::_FInit BEGIN this=%p ctg=0x%08lX cno=0x%08lX",
                this, (unsigned long)ctg, (unsigned long)cno);
#endif

    PCFL pcfl = pcrf->Pcfl();
    BLCK blck;
    MTRLF mtrlf;
    KID kid;
    MTRL *pmtrlThis = this; // 3DMMv1.0: to get MTRL from BMTL
    PTMAP ptmap = pvNil;

    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData())
        return fFalse;

    if (blck.Cb() < SIZEOF(MTRLF))
        return fFalse;
    if (!blck.FReadRgb(&mtrlf, SIZEOF(MTRLF), 0))
        return fFalse;
    if (kboOther == mtrlf.bo)
        SwapBytesBom(&mtrlf, kbomMtrlf);
    if (kboCur != mtrlf.bo)
    {
        // Some old/custom movie material chunks in the wild contain a bad
        // MTRLF byte-order header.  The retail build would continue past the
        // debug-only assertion and then consume undefined material fields.
        // Recover deterministically instead: keep any child texture below,
        // but use the normal Socrates material defaults for the wrapper.
        ClearPb(&mtrlf, SIZEOF(mtrlf));
        mtrlf.bo = kboCur;
        mtrlf.osk = koskCur;
        mtrlf.brc = 0;
        mtrlf.brufKa = kbrufKaDefault;
        mtrlf.brufKd = kbrufKdDefault;
        mtrlf.brufKs = kbrufKsDefault;
        mtrlf.bIndexBase = kiclrBaseDefault;
        mtrlf.cIndexRange = kcclrDefault;
        mtrlf.rPower = krPowerDefault;
    }

    // 3DMMEx: An arbitrary 8-character string is passed to BrMaterialAllocate (to
    // 3DMMv1.0: be stored in a string pointed to by _pbmtl->identifier).  The
    // 3DMMv1.0: contents of the string are then replaced by the "this" pointer.
    SZS szsMaterialName = "12345678";
    _pbmtl = BrMaterialAllocate((char *)szsMaterialName);
    if (pvNil == _pbmtl)
    {
#if defined(BRENDER_MODERN_14)
        BrModernLog("MTRL::_FInit FAIL BrMaterialAllocate");
#endif
        return fFalse;
    }
#if defined(BRENDER_MODERN_14)
    BrModernLog("MTRL::_FInit material allocated=%p", _pbmtl);
#endif
    CopyPb(&pmtrlThis, _pbmtl->identifier, SIZEOF(PMTRL));
    _pbmtl->colour = mtrlf.brc;
    _pbmtl->ka = mtrlf.brufKa;
    _pbmtl->kd = mtrlf.brufKd;
    // 3DMMv1.0: Note: for socrates, mtrlf.brufKs should be zero
    _pbmtl->ks = mtrlf.brufKs;

    _pbmtl->power = mtrlf.rPower;
    _pbmtl->index_base = mtrlf.bIndexBase;
    _pbmtl->index_range = mtrlf.cIndexRange;
    SetTrueColorMaterialColour(_pbmtl, mtrlf.brc != 0);
    _pbmtl->opacity = kbOpaque; // 3DMMv1.0: all socrates objects are opaque

    // 3DMMv1.0: REVIEW *****: also set the BR_MATF_PRELIT flag to use prelit models
    _pbmtl->flags = BR_MATF_LIGHT | BR_MATF_SMOOTH;
#if defined(BRENDER_MODERN_14)
    if (MVIE::FSceneFlatLightingActive())
        _pbmtl->flags &= ~BR_MATF_LIGHT;
#endif

    // 4DMM: VXP2 truecolor textures take precedence over legacy TMAP.
    // Legacy movies/VXPs continue through the untouched indexed-TMAP path.
    bool fTexture = fFalse;
    if (pcfl->FGetKidChidCtg(ctg, cno, 0, kctgT24M, &kid))
    {
#if defined(BRENDER_MODERN_14)
        BrModernLog("MTRL::_FInit VXP2 T24M child cno=%ld", (long)kid.cki.cno);
        ptmap = PtmapRead4DMMTrueColor(pcfl, kid.cki.cno);
        if (ptmap == pvNil)
        {
            BrModernLog("MTRL::_FInit FAIL VXP2 truecolor texture fetch");
            return fFalse;
        }
        fTexture = fTrue;
#else
        return fFalse;
#endif
    }
    else if (pcfl->FGetKidChidCtg(ctg, cno, 0, kctgTmap, &kid))
    {
#if defined(BRENDER_MODERN_14)
        BrModernLog("MTRL::_FInit texture child ctg=0x%08lX cno=0x%08lX",
                    (unsigned long)kid.cki.ctg, (unsigned long)kid.cki.cno);
#endif
        ptmap = (PTMAP)pcrf->PbacoFetch(kid.cki.ctg, kid.cki.cno, TMAP::FReadTmap);
        if (pvNil == ptmap)
        {
#if defined(BRENDER_MODERN_14)
            BrModernLog("MTRL::_FInit FAIL texture fetch");
#endif
            return fFalse;
        }
        fTexture = fTrue;
    }

    if (fTexture)
    {
        _pbmtl->colour_map = ptmap->Pbpmp();
#if defined(BRENDER_MODERN_14)
        BrModernLog("MTRL::_FInit texture fetched tmap=%p map=%p type=%u pixels=%p stored=%p",
                    ptmap, _pbmtl->colour_map, (unsigned)_pbmtl->colour_map->type,
                    _pbmtl->colour_map->pixels, _pbmtl->colour_map->stored);
#endif
        if (BWLD::FTrueColorMode())
            _pbmtl->colour = BR_COLOUR_RGB(0xff, 0xff, 0xff);
        Assert((PTMAP)_pbmtl->colour_map->identifier == ptmap, "lost tmap!");
        AssertPo(_ptmapShadeTable, 0);
        _pbmtl->index_shade = _ptmapShadeTable->Pbpmp();
        _pbmtl->flags |= BR_MATF_MAP_COLOUR;
        _pbmtl->index_base = 0;
        _pbmtl->index_range = _ptmapShadeTable->Pbpmp()->height - 1;
        SetTrueColorMappedMaterial(_pbmtl);

        /* 3DMMv1.0: Look for a texture transform for the MTRL */
        if (pcfl->FGetKidChidCtg(ctg, cno, 0, kctgTxxf, &kid))
        {
            TXXFF txxff;

            if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck) || !blck.FUnpackData())
                goto LFail;
            if (blck.Cb() < SIZEOF(TXXFF))
                goto LFail;
            if (!blck.FReadRgb(&txxff, SIZEOF(TXXFF), 0))
                goto LFail;
            if (kboCur != txxff.bo)
                SwapBytesBom(&txxff, kbomTxxff);
            Assert(kboCur == txxff.bo, "bad TXXFF");
            _pbmtl->map_transform = txxff.bmat23;
        }
    }
#if defined(BRENDER_MODERN_14)
    EnsureModernMaterialPixelmapsStored(_pbmtl);
    BrModernLog("MTRL::_FInit BrMaterialAdd BEGIN material=%p flags=0x%08lX colour_map=%p colour_map_stored=%p index_shade=%p index_shade_stored=%p stored=%p",
                _pbmtl, (unsigned long)_pbmtl->flags, _pbmtl->colour_map,
                _pbmtl->colour_map != pvNil ? _pbmtl->colour_map->stored : pvNil,
                _pbmtl->index_shade,
                _pbmtl->index_shade != pvNil ? _pbmtl->index_shade->stored : pvNil,
                _pbmtl->stored);
#endif
    BrMaterialAdd(_pbmtl);
#if defined(BRENDER_MODERN_14)
    BrModernLog("MTRL::_FInit BrMaterialAdd returned material=%p stored=%p colour_map_stored=%p index_shade_stored=%p",
                _pbmtl, _pbmtl->stored,
                _pbmtl->colour_map != pvNil ? _pbmtl->colour_map->stored : pvNil,
                _pbmtl->index_shade != pvNil ? _pbmtl->index_shade->stored : pvNil);
    BrModernLog("MTRL::_FInit SUCCESS this=%p", this);
#endif
    AssertThis(0);
    return fTrue;
LFail:
#if defined(BRENDER_MODERN_14)
    BrModernLog("MTRL::_FInit FAIL texture-transform/material cleanup");
#endif
    /* 3DMMv1.0: REVIEW ***** (peted): Only the code that I added uses this LFail
        case.  It's my opinion that any API which can fail should clean up
        after itself.  It happens that in the case of this MTRL class, when
        the caller releases this instance, the TMAP and BMTL are freed anyway,
        but I don't think that it's good to count on that */
    ReleasePpo(&ptmap);
    _pbmtl->colour_map = pvNil;
    BrMaterialFree(_pbmtl);
    _pbmtl = pvNil;
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Read a PIX and build a PMTRL from it
***************************************************************************/
PMTRL MTRL::PmtrlNewFromPix(PFNI pfni)
{
    AssertPo(pfni, ffniFile);

    STN stn;
    PMTRL pmtrl;
    PBMTL pbmtl;
    PTMAP ptmap;
    SZS szsMaterialName = "12345678";

    pmtrl = NewObj MTRL;
    if (pvNil == pmtrl)
        goto LFail;

    // 3DMMEx: An arbitrary 8-character string is passed to BrMaterialAllocate (to
    // 3DMMv1.0: be stored in a string pointed to by _pbmtl->identifier).  The
    // 3DMMv1.0: contents of the string are then replaced by the "this" pointer.
    pmtrl->_pbmtl = BrMaterialAllocate((char *)szsMaterialName);
    if (pvNil == pmtrl->_pbmtl)
        goto LFail;
    pbmtl = pmtrl->_pbmtl;
    CopyPb(&pmtrl, pbmtl->identifier, SIZEOF(PMTRL));
    pbmtl->colour = BWLD::FTrueColorMode() ? BR_COLOUR_RGB(0xff, 0xff, 0xff) : 0;
    pbmtl->ka = kbrufKaDefault;
    pbmtl->kd = kbrufKdDefault;
    pbmtl->ks = kbrufKsDefault;
    pbmtl->power = krPowerDefault;
    pbmtl->opacity = kbOpaque; // 3DMMv1.0: all socrates objects are opaque
    pbmtl->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD;
#if defined(BRENDER_MODERN_14)
    if (MVIE::FSceneFlatLightingActive())
        pbmtl->flags &= ~BR_MATF_LIGHT;
#endif
    pfni->GetStnPath(&stn);
    SZS szs;
    stn.GetSzs(szs);
    pbmtl->colour_map = BrPixelmapLoad(szs);
    if (pvNil == pbmtl->colour_map)
        goto LFail;

    // 3DMMv1.0: Create a TMAP for this BPMP.  We don't directly save
    // 3DMMv1.0: the ptmap...it's automagically attached to the
    // 3DMMv1.0: BPMP's identifier.
    ptmap = TMAP::PtmapNewFromBpmp(pbmtl->colour_map);
    if (pvNil == ptmap)
    {
        BrPixelmapFree(pbmtl->colour_map);
        goto LFail;
    }
    Assert((PTMAP)pbmtl->colour_map->identifier == ptmap, "lost our TMAP!");
    AssertPo(_ptmapShadeTable, 0);
    pbmtl->index_shade = _ptmapShadeTable->Pbpmp();
    pbmtl->flags |= BR_MATF_MAP_COLOUR;
    pbmtl->index_base = 0;
    pbmtl->index_range = _ptmapShadeTable->Pbpmp()->height - 1;
    if (pbmtl->colour_map->type == BR_PMT_RGB_888)
        SetTrueColorMappedMaterial(pbmtl);
    AssertPo(pmtrl, 0);
    return pmtrl;
LFail:
    ReleasePpo(&pmtrl);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Read a BMP and build a PMTRL from it
***************************************************************************/
PMTRL MTRL::PmtrlNewFromBmp(PFNI pfni, PGL pglclr)
{
    AssertPo(pfni, ffniFile);
    AssertPo(_ptmapShadeTable, 0);

    PMTRL pmtrl;
    PTMAP ptmap;

    pmtrl = PmtrlNew();
    if (pvNil == pmtrl)
        return pvNil;

    ptmap = TMAP::PtmapReadNative(pfni, pglclr);
    if (pvNil == ptmap)
    {
        ReleasePpo(&pmtrl);
        return pvNil;
    }
    pmtrl->_pbmtl->index_base = 0;
    pmtrl->_pbmtl->index_range = _ptmapShadeTable->Pbpmp()->height - 1;
    pmtrl->_pbmtl->index_shade = _ptmapShadeTable->Pbpmp();
    pmtrl->_pbmtl->flags |= BR_MATF_MAP_COLOUR;
    pmtrl->_pbmtl->colour_map = ptmap->Pbpmp();
    SetTrueColorMappedMaterial(pmtrl->_pbmtl);
    // 3DMMv1.0: The reference for ptmap has been transfered to pmtrl by the previous
    // 3DMMv1.0: line, so I don't need to ReleasePpo(&ptmap) in this function.

    return pmtrl;
}

/** 3DMMv1.0: *************************************************************************
    Return a pointer to the MTRL that owns this BMTL
***************************************************************************/
PMTRL MTRL::PmtrlFromBmtl(PBMTL pbmtl)
{
    AssertVarMem(pbmtl);

    PMTRL pmtrl = (PMTRL) * (uintptr_t *)pbmtl->identifier;
    AssertPo(pmtrl, 0);
    return pmtrl;
}

/** 3DMMv1.0: *************************************************************************
    Return this MTRL's TMAP, or pvNil if it's a solid-color MTRL.
    Note: This function doesn't AssertThis because it gets called on
    objects which are not necessarily valid (e.g., from the destructor and
    from AssertThis())
***************************************************************************/
PTMAP MTRL::Ptmap(void)
{
    AssertBaseThis(0);

    if (pvNil == _pbmtl)
        return pvNil;
    else if (pvNil == _pbmtl->colour_map)
        return pvNil;
    else
        return (PTMAP)_pbmtl->colour_map->identifier;
}

/** 3DMMv1.0: *************************************************************************
    Write a MTRL to a chunky file
***************************************************************************/
bool MTRL::FWrite(PCFL pcfl, CTG ctg, CNO *pcno)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    AssertVarMem(pcno);

    MTRLF mtrlf;
    CNO cnoChild;
    PTMAP ptmap;

    mtrlf.bo = kboCur;
    mtrlf.osk = koskCur;
    mtrlf.brc = _pbmtl->colour;
    mtrlf.brufKa = _pbmtl->ka;
    mtrlf.brufKd = _pbmtl->kd;
    mtrlf.brufKs = _pbmtl->ks;
    mtrlf.bIndexBase = _pbmtl->index_base;
    mtrlf.cIndexRange = _pbmtl->index_range;
    mtrlf.rPower = _pbmtl->power;

    if (!pcfl->FAddPv(&mtrlf, SIZEOF(MTRLF), ctg, pcno))
        return fFalse;
    ptmap = Ptmap();
    if (pvNil != ptmap)
    {
        if (!ptmap->FWrite(pcfl, kctgTmap, &cnoChild))
        {
            pcfl->Delete(ctg, *pcno);
            return fFalse;
        }
        if (!pcfl->FAdoptChild(ctg, *pcno, kctgTmap, cnoChild, 0))
        {
            pcfl->Delete(kctgTmap, cnoChild);
            pcfl->Delete(ctg, *pcno);
            return fFalse;
        }
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Free the MTRL
***************************************************************************/
MTRL::~MTRL(void)
{
    AssertBaseThis(0);

    PTMAP ptmap;

    ptmap = Ptmap();

    if (pvNil != ptmap)
    {
        ReleasePpo(&ptmap);
        _pbmtl->colour_map = pvNil;
    }
    BrMaterialRemove(_pbmtl);
    BrMaterialFree(_pbmtl);
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the MTRL.
***************************************************************************/
void MTRL::AssertValid(uint32_t grf)
{
    MTRL_PAR::AssertValid(fobjAllocated);

    AssertNilOrPo(Ptmap(), 0);
    Assert(pvNil != _ptmapShadeTable, "Why do we have MTRLs but no shade table?");
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the MTRL
***************************************************************************/
void MTRL::MarkMem(void)
{
    AssertThis(0);

    PTMAP ptmap;

    MTRL_PAR::MarkMem();
    ptmap = Ptmap();
    if (pvNil != ptmap)
        MarkMemObj(ptmap);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the shade table
***************************************************************************/
void MTRL::MarkShadeTable(void)
{
    MarkMemObj(_ptmapShadeTable);
}

#endif // 3DMMv1.0: DEBUG

//
//
//
// 3DMMv1.0:  CMTL (custom material) stuff begins here
//
//
//

/** 3DMMv1.0: *************************************************************************
    Static function to see if the given chunk has MODL children
***************************************************************************/
bool CMTL::FHasModels(PCFL pcfl, CTG ctg, CNO cno)
{
    AssertPo(pcfl, 0);

    KID kid;

    return pcfl->FGetKidChidCtg(ctg, cno, 0, kctgBmdl, &kid);
}

/** 3DMMv1.0: *************************************************************************
    Static function to see if the two given CMTLs have the same child
    MODLs
***************************************************************************/
bool CMTL::FEqualModels(PCFL pcfl, CNO cno1, CNO cno2)
{
    AssertPo(pcfl, 0);

    CHID chid = 0;
    KID kid1;
    KID kid2;

    while (pcfl->FGetKidChidCtg(kctgCmtl, cno1, chid, kctgBmdl, &kid1))
    {
        if (!pcfl->FGetKidChidCtg(kctgCmtl, cno2, chid, kctgBmdl, &kid2))
            return fFalse;
        if (kid1.cki.cno != kid2.cki.cno)
            return fFalse;
        chid++;
    }
    // 3DMMv1.0: End of cno1's BMDLs...make sure cno2 doesn't have any more
    if (pcfl->FGetKidChidCtg(kctgCmtl, cno2, chid, kctgBmdl, &kid2))
        return fFalse;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Create a new custom material
***************************************************************************/
PCMTL CMTL::PcmtlNew(int32_t ibset, int32_t cbprt, PMTRL *prgpmtrl)
{
    AssertPvCb(prgpmtrl, LwMul(cbprt, SIZEOF(PMTRL)));
    PCMTL pcmtl;
    int32_t imtrl;

    pcmtl = NewObj CMTL;
    if (pvNil == pcmtl)
        return pvNil;

    pcmtl->_ibset = ibset;
    pcmtl->_cbprt = cbprt;
    if (!FAllocPv((void **)&pcmtl->_prgpmtrl, LwMul(pcmtl->_cbprt, SIZEOF(PMTRL)), fmemClear, mprNormal))
    {
        ReleasePpo(&pcmtl);
        return pvNil;
    }
    if (!FAllocPv((void **)&pcmtl->_prgpmodl, LwMul(pcmtl->_cbprt, SIZEOF(PMODL)), fmemClear, mprNormal))
    {
        ReleasePpo(&pcmtl);
        return pvNil;
    }
    for (imtrl = 0; imtrl < cbprt; imtrl++)
    {
        AssertPo(prgpmtrl[imtrl], 0);
        pcmtl->_prgpmtrl[imtrl] = prgpmtrl[imtrl];
        pcmtl->_prgpmtrl[imtrl]->AddRef();
    }
    AssertPo(pcmtl, 0);
    return pcmtl;
}

/** 3DMMv1.0: *************************************************************************
    A PFNRPO to read CMTL objects.
***************************************************************************/
bool CMTL::FReadCmtl(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    PCMTL pcmtl;

    *pcb = SIZEOF(CMTL);
    if (pvNil == ppbaco)
        return fTrue;
    pcmtl = NewObj CMTL;
    if (pvNil == pcmtl || !pcmtl->_FInit(pcrf, ctg, cno))
    {
        ReleasePpo(&pcmtl);
        TrashVar(ppbaco);
        TrashVar(pcb);
        return fFalse;
    }
    AssertPo(pcmtl, 0);
    *ppbaco = pcmtl;
    *pcb += LwMul(SIZEOF(PMTRL) + SIZEOF(PMODL), pcmtl->_cbprt);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read a CMTL from file
***************************************************************************/
bool CMTL::_FInit(PCRF pcrf, CTG ctg, CNO cno)
{
    AssertBaseThis(0);
    AssertPo(pcrf, 0);

    int32_t ikid;
    int32_t imtrl;
    KID kid;
    BLCK blck;
    PCFL pcfl = pcrf->Pcfl();
    CMTLF cmtlf;

    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData())
        return fFalse;

    if (blck.Cb() != SIZEOF(CMTLF))
    {
        Bug("bad CMTLF...you may need to update tmpls.chk");
        return fFalse;
    }
    if (!blck.FReadRgb(&cmtlf, SIZEOF(CMTLF), 0))
        return fFalse;
    if (kboOther == cmtlf.bo)
        SwapBytesBom(&cmtlf, kbomCmtlf);
    Assert(kboCur == cmtlf.bo, "bad CMTLF");
    _ibset = cmtlf.ibset;

    // 3DMMv1.0: Highest chid is number of body part sets - 1
    _cbprt = 0;
    // 3DMMv1.0: note: there might be a faster way to compute _cbprt
    for (ikid = 0; pcfl->FGetKid(ctg, cno, ikid, &kid); ikid++)
    {
        if ((int32_t)kid.chid > (_cbprt - 1))
            _cbprt = kid.chid + 1;
    }
    if (!FAllocPv((void **)&_prgpmtrl, LwMul(_cbprt, SIZEOF(PMTRL)), fmemClear, mprNormal))
    {
        return fFalse;
    }
    if (!FAllocPv((void **)&_prgpmodl, LwMul(_cbprt, SIZEOF(PMODL)), fmemClear, mprNormal))
    {
        return fFalse;
    }
    for (imtrl = 0; imtrl < _cbprt; imtrl++)
    {
        if (pcfl->FGetKidChidCtg(ctg, cno, imtrl, kctgMtrl, &kid))
        {
            _prgpmtrl[imtrl] = (MTRL *)pcrf->PbacoFetch(kid.cki.ctg, kid.cki.cno, MTRL::FReadMtrl);
            if (pvNil == _prgpmtrl[imtrl])
                return fFalse;
        }
        if (pcfl->FGetKidChidCtg(ctg, cno, imtrl, kctgBmdl, &kid))
        {
            _prgpmodl[imtrl] = (MODL *)pcrf->PbacoFetch(kid.cki.ctg, kid.cki.cno, MODL::FReadModl);
            if (pvNil == _prgpmodl[imtrl])
                return fFalse;
        }
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Free the CMTL
***************************************************************************/
CMTL::~CMTL(void)
{
    AssertBaseThis(0);

    int32_t imtrl;

    if (pvNil != _prgpmtrl)
    {
        for (imtrl = 0; imtrl < _cbprt; imtrl++)
            ReleasePpo(&_prgpmtrl[imtrl]);
        FreePpv((void **)&_prgpmtrl);
    }

    if (pvNil != _prgpmodl)
    {
        for (imtrl = 0; imtrl < _cbprt; imtrl++)
            ReleasePpo(&_prgpmodl[imtrl]);
        FreePpv((void **)&_prgpmodl);
    }
}

/** 3DMMv1.0: *************************************************************************
    Return ibmtl'th BMTL
***************************************************************************/
BMTL *CMTL::Pbmtl(int32_t ibmtl)
{
    AssertThis(0);
    AssertIn(ibmtl, 0, _cbprt);

    return _prgpmtrl[ibmtl]->Pbmtl();
}

/** 3DMMv1.0: *************************************************************************
    Return imodl'th MODL
***************************************************************************/
PMODL CMTL::Pmodl(int32_t imodl)
{
    AssertThis(0);
    AssertIn(imodl, 0, _cbprt);

    AssertNilOrPo(_prgpmodl[imodl], 0);
    return _prgpmodl[imodl];
}

/** 3DMMv1.0: *************************************************************************
    Returns whether this CMTL has any models attached
***************************************************************************/
bool CMTL::FHasModels(void)
{
    AssertThis(0);

    int32_t imodl;

    for (imodl = 0; imodl < _cbprt; imodl++)
    {
        if (pvNil != _prgpmodl[imodl])
            return fTrue;
    }
    return fFalse;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the CMTL
***************************************************************************/
void CMTL::AssertValid(uint32_t grf)
{
    int32_t imtrl;

    MTRL_PAR::AssertValid(fobjAllocated);
    AssertPvCb(_prgpmtrl, LwMul(_cbprt, SIZEOF(MTRL *)));
    AssertPvCb(_prgpmodl, LwMul(_cbprt, SIZEOF(MODL *)));

    for (imtrl = 0; imtrl < _cbprt; imtrl++)
    {
        AssertPo(_prgpmtrl[imtrl], 0);
        AssertNilOrPo(_prgpmodl[imtrl], 0);
    }
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the MTRL
***************************************************************************/
void CMTL::MarkMem(void)
{
    AssertThis(0);

    int32_t imtrl;

    MTRL_PAR::MarkMem();
    MarkPv(_prgpmtrl);
    MarkPv(_prgpmodl);
    for (imtrl = 0; imtrl < _cbprt; imtrl++)
    {
        MarkMemObj(_prgpmtrl[imtrl]);
        MarkMemObj(_prgpmodl[imtrl]);
    }
}
#endif // 3DMMv1.0: DEBUG
