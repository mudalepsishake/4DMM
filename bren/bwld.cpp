/** 3DMMv1.0: *************************************************************************

    bwld.cpp: BRender world class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    To improve performance, BWLD can render into a reduced area, then
    enlarge the resulting image	at display time.  _fHalfX reduces the
    horizontal resolution by half, and _fHalfY reduces the vertical
    resolution by half.  Both modes can be used together to render 1/4 as
    many pixels.  _rcBuffer is the area being rendered into; _rcView is
    the area to copy _rcBuffer into (with stretching, if necessary).
    _pregnDirtyWorking and _pregnDirtyScreen are in _rcBuffer's coordinate
    system.  However, in MarkRenderedRegn, _pregnDirtyScreen is briefly
    enlarged to _rcView's coordinate system, since the gob will be drawn at
    full view resolution.

***************************************************************************/
#include "bren.h"
#include <stdint.h>
#include <stdlib.h>

#if !defined(BRENDER_MODERN_14)
extern "C" void BrZbUseFullDepthBuffer(uint32_t *prgdwDepth);
#else
// 4DMM's full-depth sidecar is a patch to the 1995 software Z renderer.
// Modern BRender does not expose that private rasterizer hook; its hardware
// path owns depth precision, so keep the old call sites harmless during the
// compatibility bring-up.
static void BrZbUseFullDepthBuffer(uint32_t *)
{
}
#endif

extern void DiagSetBRenderPhase(const char *pszPhase);
extern void DiagLogBRender(const char *pszFormat, ...);
extern bool FPerformanceProfileEnabled(void);
extern uint64_t QwPerformanceProfileNow(void);
extern uint32_t CusecPerformanceProfileElapsed(uint64_t qwStart);
extern void RecordPerformanceBRender(uint32_t cusecBeginCallbacks, uint32_t cusecClean,
                                     uint32_t cusecSceneRender, uint32_t cusecPost,
                                     uint32_t cusecTotal, int32_t cWorldChildren);
extern void RecordPerformance3DFixCamera(int32_t xr, int32_t yr, int32_t zr,
                                         int32_t zrHither, int32_t zrYon);

#if defined(BRENDER_MODERN_14)
#define MODERN_BR_BWLD_LOG(...) BrModernLog(__VA_ARGS__)
#else
#define MODERN_BR_BWLD_LOG(...) ((void)0)
#endif

#if defined(KAUAI_WIN32)
void AppMirrorViewportFromGnv(PGNV pgnvSrc, RC *prcSrc, RC *prcDst);
#endif // KAUAI_WIN32

// 3DMMv1.0: REVIEW *****: _pgptStretch is completely unused...remove it!

ASSERTNAME

RTCLASS(BWLD)

const int32_t kcbitPixelRGBIndexed = 8;
const int32_t kcbitPixelRGBTrue = 24;
const int32_t kcbPixelRGBIndexed = 1;
const int32_t kcbPixelRGBTrue = 3;

static int32_t CbitPixelRGB(void)
{
    return BWLD::FTrueColorMode() ? kcbitPixelRGBTrue : kcbitPixelRGBIndexed;
}

static int32_t CbPixelRGB(void)
{
    return BWLD::FTrueColorMode() ? kcbPixelRGBTrue : kcbPixelRGBIndexed;
}

static uint8_t BpmtRGB(void)
{
    return BWLD::FTrueColorMode() ? BR_PMT_RGB_888 : BR_PMT_INDEX_8;
}

#if defined(BRENDER_MODERN_14)
static uint32_t DwModernSparseBufferHash(const uint8_t *pbBase, int32_t cbRow, int32_t dxp, int32_t dyp,
                                         int32_t cbPixel, uint32_t *pcNonZero)
{
    if (pcNonZero != pvNil)
        *pcNonZero = 0;
    if (pbBase == pvNil || cbRow <= 0 || dxp <= 0 || dyp <= 0 || cbPixel <= 0)
        return 0;

    const int32_t dyStep = dyp > 16 ? dyp / 16 : 1;
    const int32_t dxStep = dxp > 32 ? dxp / 32 : 1;
    uint32_t dwHash = 2166136261u;
    uint32_t cNonZero = 0;
    for (int32_t yp = 0; yp < dyp; yp += dyStep)
    {
        const uint8_t *pbRow = pbBase + LwMul(yp, cbRow);
        for (int32_t xp = 0; xp < dxp; xp += dxStep)
        {
            const uint8_t *pb = pbRow + LwMul(xp, cbPixel);
            for (int32_t ib = 0; ib < cbPixel; ++ib)
            {
                const uint8_t b = pb[ib];
                if (b != 0)
                    ++cNonZero;
                dwHash ^= b;
                dwHash *= 16777619u;
            }
        }
    }
    if (pcNonZero != pvNil)
        *pcNonZero = cNonZero;
    return dwHash;
}

/***************************************************************************
    Modern glrend does not implement the legacy per-model render-bounds
    callback that 3DMM uses to populate BODY::_rcBounds.  That callback is
    not just a dirty-rectangle optimization: actor placement uses it to
    decide whether the newly placed actor is still visible.  Reconstruct a
    conservative screen-space rectangle from each rendered model's BRender
    bounds so the existing BODY callback contract remains intact.
***************************************************************************/
static bool FModernActorModelScreenBounds(PBACT pbact, PBACT pbactCamera, int32_t dxp, int32_t dyp, RC *prc)
{
    if (pbact == pvNil || pbactCamera == pvNil || prc == pvNil || dxp <= 0 || dyp <= 0)
        return fFalse;
    if (pbact->type != BR_ACTOR_MODEL || pbact->model == pvNil || pbact->render_style == BR_RSTYLE_NONE)
        return fFalse;
    if (pbact->model->nvertices == 0 || pbact->model->nfaces == 0)
        return fFalse;

    const BRB &brb = pbact->model->bounds;
    BMAT4 bmat4ActorToScreen;
    BrActorToScreenMatrix4(&bmat4ActorToScreen, pbact, pbactCamera);

    bool fHaveProjected = fFalse;
    float xpMin = 0.0f;
    float ypMin = 0.0f;
    float xpMax = 0.0f;
    float ypMax = 0.0f;

    for (int32_t iz = 0; iz < 2; ++iz)
    {
        for (int32_t iy = 0; iy < 2; ++iy)
        {
            for (int32_t ix = 0; ix < 2; ++ix)
            {
                BVEC3 bv3;
                BVEC4 bv4;
                bv3.v[0] = ix ? brb.max.v[0] : brb.min.v[0];
                bv3.v[1] = iy ? brb.max.v[1] : brb.min.v[1];
                bv3.v[2] = iz ? brb.max.v[2] : brb.min.v[2];
                BrMatrix4ApplyP(&bv4, &bv3, &bmat4ActorToScreen);

                const float w = BrScalarToFloat(bv4.v[3]);
                if (w <= 0.00001f)
                    continue;

                const float xNdc = BrScalarToFloat(bv4.v[0]) / w;
                const float yNdc = BrScalarToFloat(bv4.v[1]) / w;
                const float xp = ((xNdc + 1.0f) * 0.5f) * (float)dxp;
                const float yp = ((1.0f - yNdc) * 0.5f) * (float)dyp;

                if (!fHaveProjected)
                {
                    xpMin = xpMax = xp;
                    ypMin = ypMax = yp;
                    fHaveProjected = fTrue;
                }
                else
                {
                    if (xp < xpMin) xpMin = xp;
                    if (xp > xpMax) xpMax = xp;
                    if (yp < ypMin) ypMin = yp;
                    if (yp > ypMax) ypMax = yp;
                }
            }
        }
    }

    if (!fHaveProjected || xpMax < 0.0f || ypMax < 0.0f || xpMin >= (float)dxp || ypMin >= (float)dyp)
        return fFalse;

    int32_t xpLeft = (int32_t)xpMin;
    int32_t ypTop = (int32_t)ypMin;
    int32_t xpRight = (int32_t)xpMax + 1;
    int32_t ypBottom = (int32_t)ypMax + 1;

    if (xpLeft < 0) xpLeft = 0;
    if (ypTop < 0) ypTop = 0;
    if (xpRight > dxp) xpRight = dxp;
    if (ypBottom > dyp) ypBottom = dyp;
    if (xpRight <= xpLeft || ypBottom <= ypTop)
        return fFalse;

    prc->Set(xpLeft, ypTop, xpRight, ypBottom);
    return !prc->FEmpty();
}

#endif

const int32_t kcbitPixelZ = 16; // 3DMMv1.0: Z buffers are 16 bits deep
const int32_t kcbPixelZ = 2;

bool BWLD::_fBRenderInited = fFalse;
bool BWLD::_fTrueColorMode = fFalse;
bool BWLD::_fActorLightMode = fFalse;
bool BWLD::_f3DFixMode = fFalse;
extern "C" {
int vg3DFixBRenderMode = 0;
}
static bool vfBRenderSceneRenderActive = fFalse;

// Runtime -multi Object Group parents are direct world children and mark
// themselves with user == self. BODY callbacks still belong to the nested
// BODY roots, not to this compatibility parent actor.
static bool F4DMMObjectGroupParentActor(PBACT pbact)
{
    return pbact != pvNil && pbact->type == BR_ACTOR_NONE && pbact->user == pbact;
}

void BWLD::Set3DFixMode(bool fEnable)
{
    _f3DFixMode = FPure(fEnable);
    vg3DFixBRenderMode = _f3DFixMode ? 1 : 0;
}

/***************************************************************************
    Switch the Z-buffer renderer between the original indexed path and the
    -c RGB888 path.  Command-line parsing can occur after an auxiliary BWLD
    has already caused BrZbBegin(INDEX_8), so changing only the flag leaves
    the old rasterizer active.  Restart the Z-buffer renderer whenever the
    mode changes; BrBegin and all loaded BRender resources remain intact.
***************************************************************************/
void BWLD::SetTrueColorMode(bool fEnable)
{
    fEnable = FPure(fEnable);
    if (_fTrueColorMode == fEnable)
        return;

    _fTrueColorMode = fEnable;
    if (_fBRenderInited)
    {
#if !defined(BRENDER_MODERN_14)
        BrZbEnd();
        BrZbBegin(BpmtRGB(), BR_PMT_DEPTH_16);
#endif
    }
}

/** 3DMMv1.0: *************************************************************************
    Allocate a new BRender world
***************************************************************************/
PBWLD BWLD::PbwldNew(int32_t dxp, int32_t dyp, bool fHalfX, bool fHalfY)
{
    AssertIn(dxp, 1, ksuMax); // 3DMMv1.0: BPMP's width and height are ushorts
    AssertIn(dyp, 1, ksuMax);
    Assert(dxp % 2 == 0, "dxp should be even");
    Assert(dyp % 2 == 0, "dyp should be even");

    PBWLD pbwld;

    pbwld = NewObj BWLD;

    if (pbwld == pvNil || !pbwld->_FInit(dxp, dyp, fHalfX, fHalfY))
    {
        ReleasePpo(&pbwld);
        return pvNil;
    }

    AssertPo(pbwld, 0);
    return pbwld;
}

/** 3DMMv1.0: *************************************************************************
    Initialize the BWLD
***************************************************************************/
bool BWLD::_FInit(int32_t dxp, int32_t dyp, bool fHalfX, bool fHalfY)
{
    AssertBaseThis(0);
    MODERN_BR_BWLD_LOG("BWLD::_FInit enter this=%p size=%ldx%ld half=(%d,%d) br_inited=%d truecolour=%d",
                       this, (long)dxp, (long)dyp, (int)fHalfX, (int)fHalfY,
                       (int)_fBRenderInited, (int)_fTrueColorMode);
    AssertIn(dxp, 1, ksuMax); // 3DMMv1.0: BPMP's width and height are ushorts
    AssertIn(dyp, 1, ksuMax);

    if (!_fBRenderInited)
    {
        MODERN_BR_BWLD_LOG("BWLD::_FInit BrBegin BEGIN");
        BrBegin();
        MODERN_BR_BWLD_LOG("BWLD::_FInit BrBegin returned");
#if defined(BRENDER_MODERN_14)
        BrModernInstallDiagHandler();
        MODERN_BR_BWLD_LOG("BWLD::_FInit modern diagnostic handler installed");
#endif
#if !defined(BRENDER_MODERN_14)
        BrZbBegin(BpmtRGB(), BR_PMT_DEPTH_16);
#endif
        _fBRenderInited = fTrue;
    }

    _rcView.Set(0, 0, dxp, dyp);

    MODERN_BR_BWLD_LOG("BWLD::_FInit _FInitBuffers BEGIN");
    if (!_FInitBuffers(dxp, dyp, fHalfX, fHalfY))
    {
        MODERN_BR_BWLD_LOG("BWLD::_FInit _FInitBuffers FAILED");
        return fFalse;
    }
    MODERN_BR_BWLD_LOG("BWLD::_FInit buffers OK rgb=%p rgb_pixels=%p z=%p z_pixels=%p",
                       &_bpmpRGB, _bpmpRGB.pixels, &_bpmpZ, _bpmpZ.pixels);

    // 3DMMv1.0: Create the world and initial camera
    _bactWorld.type = BR_ACTOR_NONE;
    _bactWorld.t.type = BR_TRANSFORM_MATRIX34;
    BrMatrix34Identity(&_bactWorld.t.t.mat);
    _bactWorld.identifier = (char *)this;
    _bactCamera.type = BR_ACTOR_CAMERA;
    _bactCamera.t.type = BR_TRANSFORM_MATRIX34;
    _bactCamera.type_data = &_bcam;
    _bcam.type = BR_CAMERA_PERSPECTIVE;
    // 3DMMv1.0: Note that the aspect ratio of the view is specified rather than the
    // 3DMMv1.0: ratio of the buffer so that even when rendering into a reduced
    // 3DMMv1.0: buffer, the actors come out right when stretched to _rcView in Draw().
    _bcam.aspect = BR_SCALAR((double)_rcView.Dxp() / (double)_rcView.Dyp());
    MODERN_BR_BWLD_LOG("BWLD::_FInit BrActorAdd camera BEGIN world=%p camera=%p", &_bactWorld, &_bactCamera);
    BrActorAdd(&_bactWorld, &_bactCamera);
    MODERN_BR_BWLD_LOG("BWLD::_FInit BrActorAdd camera returned world_children=%p", _bactWorld.children);

    // 3DMMv1.0: Set up dirty region stuff
    MODERN_BR_BWLD_LOG("BWLD::_FInit REGN::PregnNew working BEGIN rc=(%ld,%ld)-(%ld,%ld)",
                       (long)_rcBuffer.xpLeft, (long)_rcBuffer.ypTop,
                       (long)_rcBuffer.xpRight, (long)_rcBuffer.ypBottom);
    _pregnDirtyWorking = REGN::PregnNew(&_rcBuffer);
    MODERN_BR_BWLD_LOG("BWLD::_FInit dirtyWorking=%p", _pregnDirtyWorking);
    if (pvNil == _pregnDirtyWorking)
        return fFalse;
    _pregnDirtyScreen = REGN::PregnNew(pvNil);
    MODERN_BR_BWLD_LOG("BWLD::_FInit dirtyScreen=%p", _pregnDirtyScreen);
    if (pvNil == _pregnDirtyScreen)
        return fFalse;

#ifdef BRENDER_ORIGINAL
    BrZbSetRenderBoundsCallback(_ActorRendered);
#elif defined(BRENDER_MODERN_14)
    // BRender 1.4's legacy BrZbSetRenderBoundsCallback path assumes the old
    // Z-buffer renderer has already been initialized.  The Modern/OpenGL
    // backend does not use BrZbBegin, so calling this during BWLD startup
    // dereferences legacy renderer state before glrend has been created.
    // The modern path already computes dirty regions after rendering, and
    // during bring-up we explicitly dirty the full CPU readback frame below.
    MODERN_BR_BWLD_LOG("BWLD::_FInit skipping legacy BrZbSetRenderBoundsCallback for BRender 1.4/OpenGL");
#else  // Source BRender 1.3.x
    MODERN_BR_BWLD_LOG("BWLD::_FInit BrZbSetRenderBoundsCallback BEGIN");
    BrZbSetRenderBoundsCallback(_ActorRenderedNew);
    MODERN_BR_BWLD_LOG("BWLD::_FInit BrZbSetRenderBoundsCallback returned");
#endif // BRender backend

    MODERN_BR_BWLD_LOG("BWLD::_FInit SUCCESS this=%p world_children=%p", this, _bactWorld.children);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Initialize or reinitialize members that depend on the values of _fHalfX
    and _fHalfY.  This function gets called by _FInit, and again every time
    FSetHalfMode is called.
***************************************************************************/
bool BWLD::_FInitBuffers(int32_t dxp, int32_t dyp, bool fHalfX, bool fHalfY)
{
    AssertBaseThis(0);
    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers enter this=%p requested=%ldx%ld half=(%d,%d)",
                       this, (long)dxp, (long)dyp, (int)fHalfX, (int)fHalfY);
    AssertIn(dxp, 1, ksuMax); // 3DMMv1.0: BPMP's width and height are ushorts
    AssertIn(dyp, 1, ksuMax);

    _fHalfX = fHalfX;
    _fHalfY = fHalfY;
    if (_fHalfX)
        dxp /= 2;
    if (_fHalfY)
        dyp /= 2;

    _rcBuffer.Set(0, 0, dxp, dyp);

    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers background Z create BEGIN %ldx%ld", (long)dxp, (long)dyp);
    ReleasePpo(&_pzbmpBackground);
    _pzbmpBackground = ZBMP::PzbmpNew(dxp, dyp);
    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers background Z=%p", _pzbmpBackground);
    if (pvNil == _pzbmpBackground)
        return fFalse;

    // 3DMMv1.0: Set up the working Z-buffer
    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers working Z create BEGIN");
    ReleasePpo(&_pzbmpWorking);
    _pzbmpWorking = ZBMP::PzbmpNew(dxp, dyp);
    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers working Z=%p pixels=%p", _pzbmpWorking,
                       _pzbmpWorking != pvNil ? _pzbmpWorking->Prgb() : pvNil);
    if (pvNil == _pzbmpWorking)
        return fFalse;
    Assert(kcbitPixelZ == 16, "change _bpmpZ.type");
    _bpmpZ.type = BR_PMT_DEPTH_16;
    _bpmpZ.row_bytes = (int16_t)LwMul(dxp, kcbPixelZ);
    _bpmpZ.width = (uint16_t)dxp;
    _bpmpZ.height = (uint16_t)dyp;
    _bpmpZ.origin_x = dxp / 2;
    _bpmpZ.origin_y = dyp / 2;
    _bpmpZ.pixels = _pzbmpWorking->Prgb();

    // 3DMMv1.0: Set up background RGB buffer
    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers background RGB create BEGIN bits=%ld", (long)CbitPixelRGB());
    ReleasePpo(&_pgptBackground);
    _pgptBackground = GPT::PgptNewOffscreen(&_rcBuffer, CbitPixelRGB());
    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers background RGB=%p", _pgptBackground);
    if (pvNil == _pgptBackground)
        return fFalse;

    // 3DMMv1.0: Set up the working RGB buffer
    if (pvNil != _pgptWorking)
    {
        _pgptWorking->Unlock();
        ReleasePpo(&_pgptWorking);
    }
    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers working RGB create BEGIN bits=%ld", (long)CbitPixelRGB());
    _pgptWorking = GPT::PgptNewOffscreen(&_rcBuffer, CbitPixelRGB());
    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers working RGB=%p", _pgptWorking);
    if (pvNil == _pgptWorking)
        return fFalse;
#if defined(BRENDER_MODERN_14)
    // Modern BRender treats the first public br_pixelmap word as the device
    // dispatch pointer when compatibility pixelmap operations are used.  The
    // old 3DMM wrapper never initialized it because 1995 BRender did not need
    // one.  Zero it explicitly so _CheckDispatch() installs the safe memory
    // pixelmap implementation instead of interpreting stale BWLD bytes as a
    // function table.
    _bpmpRGB._reserved = 0;
    _bpmpRGB.identifier = pvNil;
    _bpmpRGB.map = pvNil;
    _bpmpRGB.flags = 0;
    _bpmpRGB.base_x = 0;
    _bpmpRGB.base_y = 0;
    _bpmpRGB.user = pvNil;
    _bpmpRGB.stored = pvNil;
#endif
    _bpmpRGB.type = BpmtRGB();
    _bpmpRGB.row_bytes = (int16_t)_pgptWorking->CbRow();
    _bpmpRGB.width = (uint16_t)dxp;
    _bpmpRGB.height = (uint16_t)dyp;
    _bpmpRGB.origin_x = dxp / 2;
    _bpmpRGB.origin_y = dyp / 2;
    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers PrgbLockPixels BEGIN gpt=%p row=%ld", _pgptWorking,
                       (long)_pgptWorking->CbRow());
    _bpmpRGB.pixels = _pgptWorking->PrgbLockPixels();
    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers PrgbLockPixels returned pixels=%p", _bpmpRGB.pixels);
#if defined(BRENDER_MODERN_14)
    if (_bpmpRGB.row_bytes == (int16_t)LwMul(dxp, CbPixelRGB()))
        _bpmpRGB.flags |= BR_PMF_LINEAR | BR_PMF_ROW_WHOLEPIXELS;
#endif

    // 3DMMv1.0: If in _fHalfY and not _fHalfX, allocated a _rcView-sized buffer
    // 3DMMv1.0: for faster blitting -- see Draw()
    ReleasePpo(&_pgptStretch);
    if (!_fHalfX && _fHalfY)
    {
        _pgptStretch = GPT::PgptNewOffscreen(&_rcView, CbitPixelRGB());
        if (pvNil == _pgptStretch)
            return fFalse;
    }

    MODERN_BR_BWLD_LOG("BWLD::_FInitBuffers SUCCESS rgb_type=%u rgb_row=%ld rgb_wh=%ux%u z_row=%ld z_wh=%ux%u",
                       (unsigned)_bpmpRGB.type, (long)_bpmpRGB.row_bytes,
                       (unsigned)_bpmpRGB.width, (unsigned)_bpmpRGB.height,
                       (long)_bpmpZ.row_bytes, (unsigned)_bpmpZ.width, (unsigned)_bpmpZ.height);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Destructor for BWLD
***************************************************************************/
BWLD::~BWLD(void)
{
    AssertBaseThis(0);

    if (pvNil != _pgptWorking)
    {
        _pgptWorking->Unlock();
        ReleasePpo(&_pgptWorking);
    }
    ReleasePpo(&_pgptStretch);
    ReleasePpo(&_pgptBackground);
    ReleasePpo(&_pzbmpWorking);
    ReleasePpo(&_pzbmpBackground);
    ReleasePpo(&_pregnDirtyWorking);
    ReleasePpo(&_pregnDirtyScreen);
    ReleasePpo(&_pcrf);
}

/** 3DMMv1.0: *************************************************************************
    Change reduced rendering mode
***************************************************************************/
bool BWLD::FSetHalfMode(bool fHalfX, bool fHalfY)
{
    AssertThis(0);

    if (FPure(_fHalfX) == FPure(fHalfX) && FPure(_fHalfY) == FPure(fHalfY))
        return fTrue;

    bool fHalfXSave = _fHalfX;
    bool fHalfYSave = _fHalfY;
    RC rcBufferSave = _rcBuffer;
    PGPT pgptWorkingSave = _pgptWorking;
    PGPT pgptStretchSave = _pgptStretch;
    PGPT pgptBackgroundSave = _pgptBackground;
    BPMP bpmpRGBSave = _bpmpRGB;
    PZBMP pzbmpWorkingSave = _pzbmpWorking;
    PZBMP pzbmpBackgroundSave = _pzbmpBackground;
    BPMP bpmpZSave = _bpmpZ;

    _pgptWorking = pvNil;
    _pgptStretch = pvNil;
    _pgptBackground = pvNil;
    _pzbmpWorking = pvNil;
    _pzbmpBackground = pvNil;

    if (!_FInitBuffers(_rcView.Dxp(), _rcView.Dyp(), fHalfX, fHalfY))
        goto LFail;

    if (pvNil != _pcrf)
    {
        // 3DMMv1.0: Reload the background at the new resolution
        if (!FSetBackground(_pcrf, _ctgRGB, _cnoRGB, _ctgZ, _cnoZ))
            goto LFail;
    }

    if (pvNil != pgptWorkingSave)
    {
        pgptWorkingSave->Unlock();
        ReleasePpo(&pgptWorkingSave);
    }
    ReleasePpo(&pgptStretchSave);
    ReleasePpo(&pgptBackgroundSave);
    ReleasePpo(&pzbmpWorkingSave);
    ReleasePpo(&pzbmpBackgroundSave);

    AssertThis(0);

    return fTrue;
LFail:
    // 3DMMv1.0: Get rid of newly allocated buffers
    if (pvNil != _pgptWorking)
    {
        _pgptWorking->Unlock();
        ReleasePpo(&_pgptWorking);
    }
    ReleasePpo(&_pgptStretch);
    ReleasePpo(&_pgptBackground);
    ReleasePpo(&_pzbmpWorking);
    ReleasePpo(&_pzbmpBackground);

    // 3DMMv1.0: restore everything
    _fHalfX = fHalfXSave;
    _fHalfY = fHalfYSave;
    _rcBuffer = rcBufferSave;
    _bpmpZ = bpmpZSave;
    _bpmpRGB = bpmpRGBSave;

    _pgptWorking = pgptWorkingSave;
    _pgptStretch = pgptStretchSave;
    _pgptBackground = pgptBackgroundSave;
    _pzbmpWorking = pzbmpWorkingSave;
    _pzbmpBackground = pzbmpBackgroundSave;

    AssertThis(0);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Completely close BRender, freeing all data structures that BRender
    knows about.  This invalidates all MODLs and MTRLs in existence.
***************************************************************************/
void BWLD::CloseBRender(void)
{
    MODERN_BR_BWLD_LOG("BWLD::CloseBRender enter inited=%d", (int)_fBRenderInited);
    if (_fBRenderInited)
    {
#if defined(BRENDER_MODERN_14)
        MODERN_BR_BWLD_LOG("BWLD::CloseBRender modern viewport shutdown BEGIN");
        BrModernViewportShutdown();
        MODERN_BR_BWLD_LOG("BWLD::CloseBRender modern viewport shutdown returned");
#else
        BrZbEnd();
#endif
        MODERN_BR_BWLD_LOG("BWLD::CloseBRender BrEnd BEGIN");
        BrEnd();
        MODERN_BR_BWLD_LOG("BWLD::CloseBRender BrEnd returned");
        _fBRenderInited = fFalse;
    }
}

/** 3DMMv1.0: *************************************************************************
    Copy pvSrc into pvDst, skipping every other short.  This is called by
    FSetBackground for each row in a ZBMP when _fHalfX is fTrue.
***************************************************************************/
inline void SqueezePb(void *pvSrc, void *pvDst, int32_t cbSrc)
{
    AssertIn(cbSrc, 0, kcbMax);
    Assert(cbSrc % (LwMul(2, kcbPixelZ)) == 0, "cbSrc is not aligned");
    AssertPvCb(pvSrc, cbSrc);
    AssertPvCb(pvDst, cbSrc / 2);

    Assert(SIZEOF(int16_t) == kcbPixelZ, 0);
    int16_t *pswSrc = (int16_t *)pvSrc;
    int16_t *pswDst = (int16_t *)pvDst;

    while (cbSrc != 0)
    {
        *pswDst++ = *pswSrc++;
        pswSrc++;
        cbSrc -= LwMul(2, kcbPixelZ);
    }
}

#if defined(BRENDER_MODERN_14)
/***************************************************************************
    Decode an indexed 3DMM MBMP directly into the RGB888 BWLD background.

    The Win32 GPT::DrawMbmp 8->24 path goes through temporary GDI surfaces and
    StretchBlt.  That was a sensible 1995 presentation path, but it leaves the
    Modern/OpenGL bridge with a dark/invalid RGB buffer.  MBMP already exposes
    its decoded 8-bit indices, so expand those indices through the active 3DMM
    palette explicitly.  This is the same representation conversion used by
    the true-colour TMAP path.
***************************************************************************/
static bool FModernDrawBackgroundRgb888(PMBMP pmbmp, PGPT pgptDst, RC *prcView, RC *prcBuffer)
{
    if (pmbmp == pvNil || pgptDst == pvNil || prcView == pvNil || prcBuffer == pvNil)
        return fFalse;
    if (!BWLD::FTrueColorMode() || CbPixelRGB() != 3)
        return fFalse;

    const int32_t dxpSrc = prcView->Dxp();
    const int32_t dypSrc = prcView->Dyp();
    const int32_t dxpDst = prcBuffer->Dxp();
    const int32_t dypDst = prcBuffer->Dyp();
    if (dxpSrc <= 0 || dypSrc <= 0 || dxpDst <= 0 || dypDst <= 0)
        return fFalse;

    uint8_t *prgbIndex = pvNil;
    const int32_t cbIndex = LwMul(dxpSrc, dypSrc);
    if (!FAllocPv((void **)&prgbIndex, cbIndex, fmemClear, mprNormal))
        return fFalse;

    RC rcMbmp;
    RC rcSrc(0, 0, dxpSrc, dypSrc);
    pmbmp->GetRc(&rcMbmp);
    pmbmp->Draw(prgbIndex, dxpSrc, dypSrc, -rcMbmp.xpLeft, -rcMbmp.ypTop, &rcSrc);

    CLR rgclr[256];
    ClearPb(rgclr, SIZEOF(rgclr));
    PGL pglclr = GPT::PglclrGetPalette();
    if (pglclr == pvNil || pglclr->IvMac() < 256)
    {
        MODERN_BR_BWLD_LOG("BWLD::FSetBackground explicit palette expansion FAIL palette=%p count=%ld",
                           pglclr, pglclr != pvNil ? (long)pglclr->IvMac() : -1L);
        ReleasePpo(&pglclr);
        FreePpv((void **)&prgbIndex);
        return fFalse;
    }
    for (int32_t iclr = 0; iclr < 256; ++iclr)
        pglclr->Get(iclr, &rgclr[iclr]);
    ReleasePpo(&pglclr);

    uint8_t *prgbDst = pgptDst->PrgbLockPixels();
    if (prgbDst == pvNil)
    {
        FreePpv((void **)&prgbIndex);
        return fFalse;
    }

    const int32_t cbRowDst = pgptDst->CbRow();
    for (int32_t yp = 0; yp < dypDst; ++yp)
    {
        const int32_t ypSrc = LwMin(dypSrc - 1, LwMul(yp, dypSrc) / dypDst);
        const uint8_t *pbSrcRow = prgbIndex + LwMul(ypSrc, dxpSrc);
        uint8_t *pbDstRow = prgbDst + LwMul(yp, cbRowDst);
        for (int32_t xp = 0; xp < dxpDst; ++xp)
        {
            const int32_t xpSrc = LwMin(dxpSrc - 1, LwMul(xp, dxpSrc) / dxpDst);
            const CLR &clr = rgclr[pbSrcRow[xpSrc]];
            pbDstRow[LwMul(xp, 3) + 0] = clr.bBlue;
            pbDstRow[LwMul(xp, 3) + 1] = clr.bGreen;
            pbDstRow[LwMul(xp, 3) + 2] = clr.bRed;
        }
    }

    uint32_t rgcIndex[256];
    ClearPb(rgcIndex, SIZEOF(rgcIndex));
    uint32_t cDistinctIndex = 0;
    uint32_t ibTop[4] = {0, 0, 0, 0};
    uint32_t rgcTop[4] = {0, 0, 0, 0};
    for (int32_t ib = 0; ib < cbIndex; ++ib)
        ++rgcIndex[prgbIndex[ib]];
    for (uint32_t iclr = 0; iclr < 256; ++iclr)
    {
        if (rgcIndex[iclr] != 0)
            ++cDistinctIndex;
        for (int32_t itop = 0; itop < 4; ++itop)
        {
            if (rgcIndex[iclr] > rgcTop[itop])
            {
                for (int32_t ishift = 3; ishift > itop; --ishift)
                {
                    ibTop[ishift] = ibTop[ishift - 1];
                    rgcTop[ishift] = rgcTop[ishift - 1];
                }
                ibTop[itop] = iclr;
                rgcTop[itop] = rgcIndex[iclr];
                break;
            }
        }
    }

    uint32_t cIndexNonZero = 0;
    uint32_t cRgbNonZero = 0;
    const uint32_t dwIndexHash = DwModernSparseBufferHash(prgbIndex, dxpSrc, dxpSrc, dypSrc, 1, &cIndexNonZero);
    const uint32_t dwRgbHash = DwModernSparseBufferHash(prgbDst, cbRowDst, dxpDst, dypDst, 3, &cRgbNonZero);
    MODERN_BR_BWLD_LOG("BWLD::FSetBackground explicit MBMP->RGB888 OK mbmp_rc=(%ld,%ld)-(%ld,%ld) src=%ldx%ld dst=%ldx%ld index_hash=0x%08lX index_nonzero=%lu rgb_hash=0x%08lX rgb_nonzero=%lu first_index=%u first_rgb=%02X %02X %02X",
                       (long)rcMbmp.xpLeft, (long)rcMbmp.ypTop, (long)rcMbmp.xpRight, (long)rcMbmp.ypBottom,
                       (long)dxpSrc, (long)dypSrc, (long)dxpDst, (long)dypDst,
                       (unsigned long)dwIndexHash, (unsigned long)cIndexNonZero,
                       (unsigned long)dwRgbHash, (unsigned long)cRgbNonZero,
                       (unsigned)prgbIndex[0], (unsigned)prgbDst[0], (unsigned)prgbDst[1], (unsigned)prgbDst[2]);
    MODERN_BR_BWLD_LOG("BWLD::FSetBackground MBMP index histogram distinct=%lu top=[%lu:%lu,%lu:%lu,%lu:%lu,%lu:%lu] palette_top_bgr=[%02X%02X%02X,%02X%02X%02X,%02X%02X%02X,%02X%02X%02X]",
                       (unsigned long)cDistinctIndex,
                       (unsigned long)ibTop[0], (unsigned long)rgcIndex[ibTop[0]],
                       (unsigned long)ibTop[1], (unsigned long)rgcIndex[ibTop[1]],
                       (unsigned long)ibTop[2], (unsigned long)rgcIndex[ibTop[2]],
                       (unsigned long)ibTop[3], (unsigned long)rgcIndex[ibTop[3]],
                       (unsigned)rgclr[ibTop[0]].bBlue, (unsigned)rgclr[ibTop[0]].bGreen, (unsigned)rgclr[ibTop[0]].bRed,
                       (unsigned)rgclr[ibTop[1]].bBlue, (unsigned)rgclr[ibTop[1]].bGreen, (unsigned)rgclr[ibTop[1]].bRed,
                       (unsigned)rgclr[ibTop[2]].bBlue, (unsigned)rgclr[ibTop[2]].bGreen, (unsigned)rgclr[ibTop[2]].bRed,
                       (unsigned)rgclr[ibTop[3]].bBlue, (unsigned)rgclr[ibTop[3]].bGreen, (unsigned)rgclr[ibTop[3]].bRed);

    pgptDst->Unlock();
    FreePpv((void **)&prgbIndex);
    return fTrue;
}
#endif

/** 3DMMv1.0: *************************************************************************
    Load bitmaps from the given chunks into _pgptBackground and
    _pzbmpBackground.
***************************************************************************/
bool BWLD::FSetBackground(PCRF pcrf, CTG ctgRGB, CNO cnoRGB, CTG ctgZ, CNO cnoZ)
{
    AssertThis(0);
    AssertPo(pcrf, 0);
    MODERN_BR_BWLD_LOG("BWLD::FSetBackground BEGIN this=%p RGB=(0x%08lX,0x%08lX) Z=(0x%08lX,0x%08lX) half=(%d,%d)",
                       this, (unsigned long)ctgRGB, (unsigned long)cnoRGB,
                       (unsigned long)ctgZ, (unsigned long)cnoZ, (int)_fHalfX, (int)_fHalfY);

    PMBMP pmbmpNew;
    PZBMP pzbmpNew;

    pmbmpNew = (PMBMP)pcrf->PbacoFetch(ctgRGB, cnoRGB, MBMP::FReadMbmp);
    if (pvNil == pmbmpNew)
    {
        MODERN_BR_BWLD_LOG("BWLD::FSetBackground FAIL RGB MBMP fetch");
        return fFalse;
    }
    MODERN_BR_BWLD_LOG("BWLD::FSetBackground RGB MBMP=%p", pmbmpNew);

    pzbmpNew = (PZBMP)pcrf->PbacoFetch(ctgZ, cnoZ, ZBMP::FReadZbmp);
    if (pvNil == pzbmpNew)
    {
        MODERN_BR_BWLD_LOG("BWLD::FSetBackground FAIL ZBMP fetch");
        ReleasePpo(&pmbmpNew);
        return fFalse;
    }
    MODERN_BR_BWLD_LOG("BWLD::FSetBackground ZBMP=%p cbRow=%ld", pzbmpNew, (long)pzbmpNew->CbRow());

    // 3DMMv1.0: It's nice to cache these bitmaps if we can, but they should be
    // 3DMMv1.0: tossed first when memory gets tight because they take up a lot
    // 3DMMv1.0: of space, they're pretty fast to reload, and they are reloaded
    // 3DMMv1.0: at a time when it's okay for a pause (between scenes/views).
    pmbmpNew->SetCrep(crepTossFirst);
    pzbmpNew->SetCrep(crepTossFirst);

#if defined(BRENDER_MODERN_14)
    const bool fModernRgbLoaded = FModernDrawBackgroundRgb888(pmbmpNew, _pgptBackground, &_rcView, &_rcBuffer);
    if (!fModernRgbLoaded && FTrueColorMode())
        MODERN_BR_BWLD_LOG("BWLD::FSetBackground explicit MBMP->RGB888 unavailable; falling back to legacy GPT/GDI draw");
#else
    const bool fModernRgbLoaded = fFalse;
#endif

    if (_fHalfX || _fHalfY)
    {
        // 3DMMv1.0: Need to squeeze pmbmpNew and pzbmpNew into _pgptBackground
        // and _pzbmpBackground.  Modern RGB888 expands the indexed MBMP
        // directly; the legacy path still uses the historical GPT stretch.
        int32_t yp;
        uint8_t *pbSrc;
        uint8_t *pbDst;
        int32_t cbRowSrc;
        int32_t cbRowDst;
        if (!fModernRgbLoaded)
        {
            PGPT pgptFull = GPT::PgptNewOffscreen(&_rcView, CbitPixelRGB());
            if (pvNil == pgptFull)
            {
                ReleasePpo(&pmbmpNew);
                ReleasePpo(&pzbmpNew);
                return fFalse;
            }
            GNV gnvFull(pgptFull);
            GNV gnvHalf(_pgptBackground);
            gnvFull.DrawMbmp(pmbmpNew, 0, 0);
            gnvHalf.CopyPixels(&gnvFull, &_rcView, &_rcBuffer);
            ReleasePpo(&pgptFull);
        }
        ReleasePpo(&pmbmpNew);

        pbSrc = pzbmpNew->Prgb();
        pbDst = _pzbmpBackground->Prgb();
        cbRowSrc = pzbmpNew->CbRow();
        cbRowDst = _pzbmpBackground->CbRow();
        Assert(cbRowSrc - cbRowDst == (_fHalfX ? cbRowDst : 0), "bad src/dest width ratio");
        for (yp = 0; yp < _rcBuffer.Dyp(); yp++)
        {
            if (_fHalfX)
                SqueezePb(pbSrc, pbDst, cbRowSrc);
            else
                CopyPb(pbSrc, pbDst, cbRowSrc);
            pbSrc += cbRowSrc;
            if (_fHalfY)
                pbSrc += cbRowSrc; // 3DMMv1.0: skip rows in source
            pbDst += cbRowDst;
        }
        ReleasePpo(&pzbmpNew);
    }
    else // 3DMMv1.0: not in half mode
    {
        if (!fModernRgbLoaded)
        {
            GNV gnv(_pgptBackground);
            gnv.DrawMbmp(pmbmpNew, 0, 0);
        }
        ReleasePpo(&pmbmpNew);

        ReleasePpo(&_pzbmpBackground);
        _pzbmpBackground = pzbmpNew;
    }

#if defined(BRENDER_MODERN_14)
    {
        uint8_t *pbBackground = _pgptBackground->PrgbLockPixels();
        if (pbBackground != pvNil)
        {
            uint32_t cNonZero = 0;
            const uint32_t dwHash = DwModernSparseBufferHash(pbBackground, _pgptBackground->CbRow(),
                                                              _rcBuffer.Dxp(), _rcBuffer.Dyp(),
                                                              CbPixelRGB(), &cNonZero);
            MODERN_BR_BWLD_LOG("BWLD::FSetBackground CPU background sparse_hash=0x%08lX nonzero_samples=%lu first=%02X %02X %02X %02X %02X %02X row=%ld",
                               (unsigned long)dwHash, (unsigned long)cNonZero,
                               (unsigned)pbBackground[0], (unsigned)pbBackground[1], (unsigned)pbBackground[2],
                               (unsigned)pbBackground[3], (unsigned)pbBackground[4], (unsigned)pbBackground[5],
                               (long)_pgptBackground->CbRow());
        }
        _pgptBackground->Unlock();
    }
#endif

    // 3DMMv1.0: entire working buffer is dirty because of background change
    _pregnDirtyWorking->SetRc(&_rcBuffer);
    _fWorldChanged = fTrue;

    // 3DMMv1.0: Keep a reference to the background, in case we change to/from
    // 3DMMv1.0: halfmode and need to reload it.
    pcrf->AddRef();
    ReleasePpo(&_pcrf);
    _pcrf = pcrf;
    _ctgRGB = ctgRGB;
    _cnoRGB = cnoRGB;
    _ctgZ = ctgZ;
    _cnoZ = cnoZ;

    MODERN_BR_BWLD_LOG("BWLD::FSetBackground SUCCESS this=%p", this);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Change the camera matrix
***************************************************************************/
void BWLD::SetCamera(BMAT34 *pbmat34, BRS zrHither, BRS zrYon, BRA aFov)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    Assert(zrYon > zrHither, "Yon must be further than hither");

    _bactCamera.t.t.mat = *pbmat34;
    _bcam.hither_z = zrHither;
    _bcam.yon_z = zrYon;
    _bcam.field_of_view = aFov;

    // A camera change changes every projected pixel.  The historical software
    // path could get away with carrying only the dirty working region here, but
    // the Modern renderer gates scene traversal on _fWorldChanged.  Keep the
    // BWLD contract complete so a camera-only update always produces a frame.
    _pregnDirtyWorking->SetRc(&_rcBuffer);
    _fWorldChanged = fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Get the camera matrix
***************************************************************************/
void BWLD::GetCamera(BMAT34 *pbmat34, BRS *pzrHither, BRS *pzrYon, BRA *paFov)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    AssertNilOrVarMem(pzrHither);
    AssertNilOrVarMem(pzrYon);
    AssertNilOrVarMem(paFov);

    *pbmat34 = _bactCamera.t.t.mat;
    if (pvNil != pzrHither)
        *pzrHither = _bcam.hither_z;
    if (pvNil != pzrYon)
        *pzrYon = _bcam.yon_z;
    if (pvNil != paFov)
        *paFov = _bcam.field_of_view;
}

/** 3DMMv1.0: *************************************************************************
    Render the world.  First, notify all BODYs that we're about to render,
    so they can clear their _pregn's.  Then clean the RGB and Z working
    buffers, since they're probably dirty from the last render.  Update
    some regions, and render everything.
***************************************************************************/
void BWLD::Render(void)
{
    AssertThis(0);

    PBACT pbact;
    RC rc;
    int32_t ibactDiag = 0;
    uint64_t qwPerfTotal = 0;
    uint64_t qwPerfStage = 0;
    uint32_t cusecPerfBegin = 0;
    uint32_t cusecPerfClean = 0;
    uint32_t cusecPerfScene = 0;
    uint32_t cusecPerfPost = 0;

    // actorlight39: v38's camera-relative world rebase produced no visual
    // change in the far-camera depth failure. -3dfix now lives inside Source
    // BRender as a projection/depth diagnostic instead of mutating scene
    // transforms around BrZbSceneRender.

    if (FPerformanceProfileEnabled())
        qwPerfTotal = QwPerformanceProfileNow();

    DiagSetBRenderPhase("BWLD::Render enter");
    DiagLogBRender("Render this=%p changed=%d active=%d world_children=%p working_gpt=%p background_gpt=%p rgb_pixels=%p z_pixels=%p",
                   this, (int)_fWorldChanged, (int)vfBRenderSceneRenderActive,
                   _bactWorld.children, _pgptWorking, _pgptBackground,
                   _bpmpRGB.pixels, _bpmpZ.pixels);

    if (!_fWorldChanged)
    {
        DiagSetBRenderPhase("BWLD::Render early return world clean");
        return;
    }

    // BRender owns one process-global scratchpad.  A nested render from a
    // secondary preview while the movie world is already inside BRender will
    // otherwise terminate with "Scratchpad not available".  Leave this world
    // dirty and let its normal UI tick render it after the active render exits.
    if (vfBRenderSceneRenderActive)
    {
        DiagSetBRenderPhase("BWLD::Render nested render suppressed");
        return;
    }
    vfBRenderSceneRenderActive = fTrue;

    // 3DMMv1.0: Note that we only call pfnbeginrend on immediate children of
    // 3DMMv1.0: the world, because that will hit all the BODYs in Socrates.
    DiagSetBRenderPhase("BWLD::Render before begin-render callbacks");
    if (FPerformanceProfileEnabled())
        qwPerfStage = QwPerformanceProfileNow();
    if (pvNil != _pfnbeginrend)
    {
        for (pbact = _bactWorld.children; pvNil != pbact; pbact = pbact->next, ibactDiag++)
        {
            DiagLogBRender("begin-render child=%ld actor=%p type=%u parent=%p next=%p model=%p material=%p",
                           (long)ibactDiag, pbact, (unsigned)pbact->type, pbact->parent, pbact->next,
                           pbact->model, pbact->material);
            // Normally BODY roots are immediate BR_ACTOR_NONE world children.
            // Bound Object Groups insert one shared BR_ACTOR_NONE parent above
            // them, so descend exactly one compatibility level for callbacks.
            if (F4DMMObjectGroupParentActor(pbact))
            {
                for (PBACT pbactBody = pbact->children; pbactBody != pvNil; pbactBody = pbactBody->next)
                {
                    if (pbactBody->type != BR_ACTOR_NONE || pbactBody->identifier == pvNil)
                        continue;
                    DiagSetBRenderPhase("BWLD::Render inside grouped begin-render callback");
                    _pfnbeginrend(pbactBody);
                }
            }
            else if (pbact->type == BR_ACTOR_NONE)
            {
                DiagSetBRenderPhase("BWLD::Render inside begin-render callback");
                _pfnbeginrend(pbact);
                DiagLogBRender("begin-render child=%ld callback complete", (long)ibactDiag);
            }
        }
    }

    if (FPerformanceProfileEnabled())
        cusecPerfBegin = CusecPerformanceProfileElapsed(qwPerfStage);
    DiagSetBRenderPhase("BWLD::Render before CleanWorkingBuffers");
    if (FPerformanceProfileEnabled())
        qwPerfStage = QwPerformanceProfileNow();
    _CleanWorkingBuffers();
    if (FPerformanceProfileEnabled())
        cusecPerfClean = CusecPerformanceProfileElapsed(qwPerfStage);
    DiagSetBRenderPhase("BWLD::Render after CleanWorkingBuffers");

    // 3DMMv1.0: Now the working buffer is clean, but we should mark everything that
    // we just cleaned in _CleanWorkingBuffers as dirty in the screen buffer.
    DiagSetBRenderPhase("BWLD::Render before dirty-region merge");
    _pregnDirtyScreen->FUnion(_pregnDirtyWorking);
    _pregnDirtyWorking->SetRc(pvNil);
    DiagSetBRenderPhase("BWLD::Render after dirty-region merge");

    // Render the scene.  This will add stuff to _pregnDirtyWorking.
    DiagLogBRender("BrZbSceneRender world=%p camera=%p rgb=%p rgb_pixels=%p rgb_row=%ld rgb_wh=%ux%u z=%p z_pixels=%p z_row=%ld z_wh=%ux%u",
                   &_bactWorld, &_bactCamera, &_bpmpRGB, _bpmpRGB.pixels,
                   (long)_bpmpRGB.row_bytes, (unsigned)_bpmpRGB.width, (unsigned)_bpmpRGB.height,
                   &_bpmpZ, _bpmpZ.pixels, (long)_bpmpZ.row_bytes,
                   (unsigned)_bpmpZ.width, (unsigned)_bpmpZ.height);
    DiagSetBRenderPhase("BWLD::Render inside BrZbSceneRender");

#if defined(BRENDER_MODERN_14)
    // BRender 1.4/OpenGL bring-up.  The helper uploads the already-cleaned
    // 3DMM background RGB pixels, renders the actor hierarchy on the GPU,
    // reads the result back into _bpmpRGB for the normal editor viewport,
    // and presents the canonical movie world directly in the -v window.
    // Background Z import is deliberately deferred to the next parity step.
    if (FPerformanceProfileEnabled() && _bactCamera.t.type == BR_TRANSFORM_MATRIX34)
    {
        RecordPerformance3DFixCamera((int32_t)_bactCamera.t.t.mat.m[3][0],
                                     (int32_t)_bactCamera.t.t.mat.m[3][1],
                                     (int32_t)_bactCamera.t.t.mat.m[3][2],
                                     (int32_t)_bcam.hither_z, (int32_t)_bcam.yon_z);
    }

    if (FPerformanceProfileEnabled())
        qwPerfStage = QwPerformanceProfileNow();
    const bool fModernPresent = (_rcView.Dxp() == 544 && _rcView.Dyp() == 306);
    MODERN_BR_BWLD_LOG("BWLD::Render calling FBrModernViewportRender this=%p view=%ldx%ld present=%d",
                       this, (long)_rcView.Dxp(), (long)_rcView.Dyp(), (int)fModernPresent);
    const bool fModernRendered = FBrModernViewportRender(this, &_bactWorld, &_bactCamera, &_bpmpRGB, fModernPresent);
    MODERN_BR_BWLD_LOG("BWLD::Render FBrModernViewportRender returned=%d", (int)fModernRendered);
    if (fModernRendered)
    {
        // glrend does not emit the legacy BrZb render-bounds callback.  3DMM
        // uses that callback to populate BODY::_rcBounds, which in turn is
        // authoritative for placement commit, cursor centering and several
        // selection paths.  Reconstruct those callbacks from the live actor
        // hierarchy and each model's projected bounds.
        int32_t cModernBoundsVisible = 0;
        int32_t cModernBoundsRejected = 0;
        int32_t cModernBoundsOverflow = 0;
        if (_pfnbactrend != pvNil)
        {
            PBACT rgpbactModern[2048];
            int32_t cbactModernStack = 0;
            for (PBACT pbactSeed = _bactWorld.children; pbactSeed != pvNil; pbactSeed = pbactSeed->next)
            {
                if (cbactModernStack < (int32_t)(sizeof(rgpbactModern) / sizeof(rgpbactModern[0])))
                    rgpbactModern[cbactModernStack++] = pbactSeed;
                else
                    ++cModernBoundsOverflow;
            }

            while (cbactModernStack > 0)
            {
                PBACT pbactBounds = rgpbactModern[--cbactModernStack];
                for (PBACT pbactChild = pbactBounds->children; pbactChild != pvNil; pbactChild = pbactChild->next)
                {
                    if (cbactModernStack < (int32_t)(sizeof(rgpbactModern) / sizeof(rgpbactModern[0])))
                        rgpbactModern[cbactModernStack++] = pbactChild;
                    else
                        ++cModernBoundsOverflow;
                }

                RC rcModernBounds;
                if (FModernActorModelScreenBounds(pbactBounds, &_bactCamera, _bpmpRGB.width, _bpmpRGB.height, &rcModernBounds))
                {
                    _pfnbactrend(pbactBounds, &rcModernBounds);
                    ++cModernBoundsVisible;
                    MODERN_BR_BWLD_LOG(
                        "BWLD::Render modern synthetic rendered-callback actor=%p model=%p identifier=%p bounds=(%ld,%ld)-(%ld,%ld)",
                        pbactBounds, pbactBounds->model, pbactBounds->identifier,
                        (long)rcModernBounds.xpLeft, (long)rcModernBounds.ypTop,
                        (long)rcModernBounds.xpRight, (long)rcModernBounds.ypBottom);
                }
                else if (pbactBounds->type == BR_ACTOR_MODEL && pbactBounds->model != pvNil &&
                         pbactBounds->model->nvertices != 0 && pbactBounds->model->nfaces != 0)
                {
                    ++cModernBoundsRejected;
                    MODERN_BR_BWLD_LOG(
                        "BWLD::Render modern bounds rejected actor=%p model=%p identifier=%p verts=%u faces=%u style=%u",
                        pbactBounds, pbactBounds->model, pbactBounds->identifier,
                        (unsigned)pbactBounds->model->nvertices, (unsigned)pbactBounds->model->nfaces,
                        (unsigned)pbactBounds->render_style);
                }
            }
        }
        MODERN_BR_BWLD_LOG("BWLD::Render modern bounds bridge visible=%ld rejected=%ld stack_overflow=%ld callback=%p",
                           (long)cModernBoundsVisible, (long)cModernBoundsRejected,
                           (long)cModernBoundsOverflow, _pfnbactrend);

        // Keep full-frame dirty marking for the GPU readback path.  The
        // synthetic BODY bounds above restore semantic visibility; this full
        // rectangle still guarantees presentation of every changed GPU pixel.
        _pregnDirtyWorking->FUnionRc(&_rcBuffer);
        MODERN_BR_BWLD_LOG("BWLD::Render modern GPU frame marked full buffer dirty rc=(%ld,%ld)-(%ld,%ld)",
                           (long)_rcBuffer.xpLeft, (long)_rcBuffer.ypTop,
                           (long)_rcBuffer.xpRight, (long)_rcBuffer.ypBottom);
    }
    else
        DiagLogBRender("BRender14 GPU scene render unavailable; leaving cleaned background in BWLD buffer");
    if (FPerformanceProfileEnabled())
        cusecPerfScene = CusecPerformanceProfileElapsed(qwPerfStage);
    DiagSetBRenderPhase("BWLD::Render after BRender14 glrend");
#else
    // actorlight32: Source BRender projects depth as a full 16.16 value but
    // its 1995 Z rasterizers throw away the fractional 16 bits and compare
    // only a 16-bit integer Z buffer.  Large city geometry and nearly
    // coplanar/intersecting cubes can therefore alternate winners from one
    // scanline to the next (the familiar Venetian-blind Z fighting).
    //
    // Keep the legacy 16-bit ZBMP intact for 3DMM background occlusion and
    // file compatibility, but give the RGB888 triangle rasterizers a
    // per-frame full-precision sidecar initialized from that same buffer.
    // The renderer still mirrors accepted pixels back into the original
    // 16-bit Z buffer, so every existing 3DMM path continues to see the
    // format it expects.
    uint32_t *prgdwDepthFull = NULL;
    BrZbUseFullDepthBuffer(NULL);
    if (FTrueColorMode())
    {
        const int32_t dxpDepth = _bpmpZ.width;
        const int32_t dypDepth = _bpmpZ.height;
        const size_t cpxDepth = (size_t)dxpDepth * (size_t)dypDepth;
        prgdwDepthFull = (uint32_t *)malloc(cpxDepth * sizeof(uint32_t));
        if (prgdwDepthFull != NULL)
        {
            const uint16_t *prgwDepth = (const uint16_t *)_bpmpZ.pixels;
            const int32_t czwRow = _bpmpZ.row_bytes / (int32_t)sizeof(uint16_t);
            for (int32_t ypDepth = 0; ypDepth < dypDepth; ++ypDepth)
            {
                const uint16_t *prgwRow = prgwDepth + ypDepth * czwRow;
                uint32_t *prgdwRow = prgdwDepthFull + (size_t)ypDepth * (size_t)dxpDepth;
                for (int32_t xpDepth = 0; xpDepth < dxpDepth; ++xpDepth)
                {
                    // Fill the discarded fractional bits with ones.  That
                    // preserves legacy equal-16-bit-depth behavior while
                    // allowing subsequent actor surfaces to retain their
                    // real sub-pixel depth against one another.
                    prgdwRow[xpDepth] = ((uint32_t)prgwRow[xpDepth] << 16) | 0xFFFFu;
                }
            }
            BrZbUseFullDepthBuffer(prgdwDepthFull);
        }
    }

    if (FPerformanceProfileEnabled() && _bactCamera.t.type == BR_TRANSFORM_MATRIX34)
    {
        RecordPerformance3DFixCamera((int32_t)_bactCamera.t.t.mat.m[3][0],
                                     (int32_t)_bactCamera.t.t.mat.m[3][1],
                                     (int32_t)_bactCamera.t.t.mat.m[3][2],
                                     (int32_t)_bcam.hither_z, (int32_t)_bcam.yon_z);
    }

    if (FPerformanceProfileEnabled())
        qwPerfStage = QwPerformanceProfileNow();
    BrZbSceneRender(&_bactWorld, &_bactCamera, &_bpmpRGB, &_bpmpZ);

    BrZbUseFullDepthBuffer(NULL);
    free(prgdwDepthFull);
    if (FPerformanceProfileEnabled())
        cusecPerfScene = CusecPerformanceProfileElapsed(qwPerfStage);
    DiagSetBRenderPhase("BWLD::Render after BrZbSceneRender");
#endif // BRENDER_MODERN_14

    vfBRenderSceneRenderActive = fFalse;

    ibactDiag = 0;
    if (FPerformanceProfileEnabled())
        qwPerfStage = QwPerformanceProfileNow();
    for (pbact = _bactWorld.children; pvNil != pbact; pbact = pbact->next, ibactDiag++)
    {
        DiagLogBRender("post-render child=%ld actor=%p type=%u next=%p", (long)ibactDiag,
                       pbact, (unsigned)pbact->type, pbact->next);
        // As above, callbacks belong to BODY roots even when a bound group
        // makes those roots grandchildren of BWLD's world actor.
        if (pvNil != _pfngetrect && F4DMMObjectGroupParentActor(pbact))
        {
            for (PBACT pbactBody = pbact->children; pbactBody != pvNil; pbactBody = pbactBody->next)
            {
                if (pbactBody->type != BR_ACTOR_NONE || pbactBody->identifier == pvNil)
                    continue;
                DiagSetBRenderPhase("BWLD::Render inside grouped get-rect callback");
                _pfngetrect(pbactBody, &rc);
                _pregnDirtyWorking->FUnionRc(&rc);
            }
        }
        else if (pbact->type == BR_ACTOR_NONE && pvNil != _pfngetrect)
        {
            DiagSetBRenderPhase("BWLD::Render inside get-rect callback");
            _pfngetrect(pbact, &rc);
            _pregnDirtyWorking->FUnionRc(&rc);
        }
    }

    if (FPerformanceProfileEnabled())
        cusecPerfPost = CusecPerformanceProfileElapsed(qwPerfStage);
    DiagSetBRenderPhase("BWLD::Render before final dirty union");
    _pregnDirtyScreen->FUnion(_pregnDirtyWorking);

    _fWorldChanged = fFalse;
    DiagSetBRenderPhase("BWLD::Render complete");
    if (FPerformanceProfileEnabled())
        RecordPerformanceBRender(cusecPerfBegin, cusecPerfClean, cusecPerfScene, cusecPerfPost,
                                 CusecPerformanceProfileElapsed(qwPerfTotal), ibactDiag);
}

/** 3DMMv1.0: *************************************************************************
    "Prerender" the world.  That is, render the world, then copy it into
    the background buffers
***************************************************************************/
void BWLD::Prerender(void)
{
    AssertThis(0);

    GNV gnvBackground(_pgptBackground);
    GNV gnvWorking(_pgptWorking);

    Render();

    _pzbmpWorking->Draw((uint8_t *)_pzbmpBackground->Prgb(), _pzbmpBackground->CbRow(), _rcBuffer.Dyp(), 0, 0,
                        &_rcBuffer, pvNil);

    // 3DMMv1.0: Need to detach _pzbmpBackground from the CRF so when we unprerender,
    // 3DMMv1.0: a fresh copy of the ZBMP is fetched
    _pzbmpBackground->Detach();

    gnvBackground.CopyPixels(&gnvWorking, &_rcBuffer, &_rcBuffer);

    // 3DMMv1.0: Need to ensure that the current contents of _pgptWorking (just the
    // 3DMMv1.0: prerenderable actors) go into _pgptBackground
    GPT::Flush();
}

/** 3DMMv1.0: *************************************************************************
    "Un-Prerender" the world.  That is, restore the background bitmaps to
    the way they were before prerendering any actors
***************************************************************************/
void BWLD::Unprerender(void)
{
    AssertThis(0);

    // 3DMMv1.0: Ignore error...you'll just get weird visual effects and an error
    // 3DMMv1.0: will be reported elsewhere
    FSetBackground(_pcrf, _ctgRGB, _cnoRGB, _ctgZ, _cnoZ);
}

/** 3DMMv1.0: *************************************************************************
    Copy _pregnDirtyWorking from background Z and RGB buffers to working
    Z and RGB buffers.
***************************************************************************/
void BWLD::_CleanWorkingBuffers(void)
{
    AssertThis(0);

    DiagSetBRenderPhase("BWLD::_CleanWorkingBuffers enter");

    REGSC regsc;
    int32_t yp;
    int32_t xpLeft;
    RC rcRegnBounds;
    RC rcClippedRegnBounds;
    uint8_t *pbSrc;
    uint8_t *pbDst;
    int32_t cbRowCopy;
    RC rc;
    int32_t cbRowSrc, cbRowDst;

    if (_pregnDirtyWorking->FEmpty(&rcRegnBounds))
    {
        DiagSetBRenderPhase("BWLD::_CleanWorkingBuffers no dirty region");
        return;
    }
    if (!rcClippedRegnBounds.FIntersect(&rcRegnBounds, &_rcBuffer))
    {
        DiagSetBRenderPhase("BWLD::_CleanWorkingBuffers dirty region outside buffer");
        return;
    }

    DiagLogBRender("Clean dirty=(%ld,%ld)-(%ld,%ld) clipped=(%ld,%ld)-(%ld,%ld) rgb_bytes_per_pixel=%ld",
                   (long)rcRegnBounds.xpLeft, (long)rcRegnBounds.ypTop,
                   (long)rcRegnBounds.xpRight, (long)rcRegnBounds.ypBottom,
                   (long)rcClippedRegnBounds.xpLeft, (long)rcClippedRegnBounds.ypTop,
                   (long)rcClippedRegnBounds.xpRight, (long)rcClippedRegnBounds.ypBottom,
                   (long)CbPixelRGB());

    // 3DMMv1.0: Clean the Z buffer
    DiagSetBRenderPhase("BWLD::_CleanWorkingBuffers before Z Draw");
    _pzbmpBackground->Draw((uint8_t *)_bpmpZ.pixels, _bpmpZ.row_bytes, _bpmpZ.height, 0, 0, &rcClippedRegnBounds,
                           _pregnDirtyWorking);
    DiagSetBRenderPhase("BWLD::_CleanWorkingBuffers after Z Draw");

    // 3DMMv1.0: Clean the RGB buffer
    regsc.Init(_pregnDirtyWorking, &rcClippedRegnBounds);
    yp = rcClippedRegnBounds.ypTop;
    const int32_t cbPixelRGB = CbPixelRGB();
    cbRowSrc = _pgptBackground->CbRow();
    pbSrc = _pgptBackground->PrgbLockPixels() + LwMul(yp, cbRowSrc) +
            LwMul(rcClippedRegnBounds.xpLeft, cbPixelRGB);
    cbRowDst = _pgptWorking->CbRow();
    pbDst = _pgptWorking->PrgbLockPixels() + LwMul(yp, cbRowDst) +
            LwMul(rcClippedRegnBounds.xpLeft, cbPixelRGB);
    DiagLogBRender("Clean RGB src=%p dst=%p src_row=%ld dst_row=%ld start_y=%ld",
                   pbSrc, pbDst, (long)cbRowSrc, (long)cbRowDst, (long)yp);
    DiagSetBRenderPhase("BWLD::_CleanWorkingBuffers inside RGB copy loop");
    for (; yp < rcClippedRegnBounds.ypBottom; yp++)
    {
        while (regsc.XpCur() < klwMax)
        {
            xpLeft = regsc.XpCur();
            cbRowCopy = regsc.XpFetch() - xpLeft;
            regsc.XpFetch();
            CopyPb(pbSrc + LwMul(xpLeft, cbPixelRGB), pbDst + LwMul(xpLeft, cbPixelRGB),
                   LwMul(cbRowCopy, cbPixelRGB));
        }
        pbSrc += cbRowSrc;
        pbDst += cbRowDst;
        regsc.ScanNext(1);
    }
#if defined(BRENDER_MODERN_14)
    {
        uint32_t cNonZero = 0;
        const uint32_t dwHash = DwModernSparseBufferHash((const uint8_t *)_bpmpRGB.pixels, _bpmpRGB.row_bytes,
                                                          _bpmpRGB.width, _bpmpRGB.height, CbPixelRGB(), &cNonZero);
        MODERN_BR_BWLD_LOG("BWLD::_CleanWorkingBuffers WORKING sparse_hash=0x%08lX nonzero_samples=%lu first=%02X %02X %02X %02X %02X %02X",
                           (unsigned long)dwHash, (unsigned long)cNonZero,
                           (unsigned)((uint8_t *)_bpmpRGB.pixels)[0], (unsigned)((uint8_t *)_bpmpRGB.pixels)[1],
                           (unsigned)((uint8_t *)_bpmpRGB.pixels)[2], (unsigned)((uint8_t *)_bpmpRGB.pixels)[3],
                           (unsigned)((uint8_t *)_bpmpRGB.pixels)[4], (unsigned)((uint8_t *)_bpmpRGB.pixels)[5]);
    }
#endif
    _pgptWorking->Unlock();
    _pgptBackground->Unlock();
    DiagSetBRenderPhase("BWLD::_CleanWorkingBuffers complete");
}

/** 3DMMEx: *************************************************************************
    Adapter for new function signature for the br_renderbounds_cbfn
    callback in BRender v1.3.2
***************************************************************************/
void BWLD::_ActorRenderedNew(PBACT pbact, PBMDL pbmdl, PBMTL pbmtl, void *render_data, br_uint_8 bStyle,
                             br_matrix4 *pbmat4ModelToScreen, br_int_32 bounds[4])
{
    _ActorRendered(pbact, pbmdl, pbmtl, bStyle, pbmat4ModelToScreen, bounds);
}

/** 3DMMv1.0: *************************************************************************
    Callback for when a BACT is rendered.  Need to union with dirty
    region.
***************************************************************************/
void BWLD::_ActorRendered(PBACT pbact, PBMDL pbmdl, PBMTL pbmtl, br_uint_8 bStyle, br_matrix4 *pbmat4ModelToScreen,
                          br_int_32 bounds[4])
{
    AssertVarMem(pbact);

    DiagLogBRender("rendered-callback actor=%p model=%p material=%p style=%u bounds=(%ld,%ld)-(%ld,%ld)",
                   pbact, pbmdl, pbmtl, (unsigned)bStyle, (long)bounds[0], (long)bounds[1],
                   (long)bounds[2], (long)bounds[3]);

    PBACT pbactT = pbact;
    PBWLD pbwld;
    RC rc(bounds[0], bounds[1], bounds[2] + 1, bounds[3] + 1);

    while (pbactT->parent != pvNil)
        pbactT = pbactT->parent;
    pbwld = (PBWLD)pbactT->identifier;
    AssertPo(pbwld, 0);
    if (pvNil != pbwld->_pfnbactrend)
        pbwld->_pfnbactrend(pbact, &rc); // 3DMMv1.0: call client callback
}

/** 3DMMv1.0: *************************************************************************
    Mark the region that has been rendered (and needs to be copied to the
    screen)
***************************************************************************/
void BWLD::MarkRenderedRegn(PGOB pgob, int32_t dxp, int32_t dyp)
{
    AssertThis(0);
    AssertPo(pgob, 0);

    _pregnDirtyScreen->Scale((_fHalfX ? 2 : 1), 1, (_fHalfY ? 2 : 1), 1);
    _pregnDirtyScreen->Offset(dxp, dyp);
    vpappb->MarkRegn(_pregnDirtyScreen, pgob);
    _pregnDirtyScreen->SetRc(pvNil); // 3DMMv1.0: screen is clean
}

/** 3DMMv1.0: *************************************************************************
    Draw the BWLD's working RGB buffer into pgnv.  The movie engine should
    have called BWLD::MarkRenderedRegn before calling this, so only
    _pregnDirtyScreen's bits will be copied.
***************************************************************************/
void BWLD::Draw(PGNV pgnv, RC *prcClip, int32_t dxp, int32_t dyp)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);

    RC rc;
    GNV gnvTemp(_pgptWorking);

    rc.OffsetCopy(&_rcView, -dxp, -dyp);
    pgnv->CopyPixels(&gnvTemp, &_rcBuffer, &rc);

#if defined(KAUAI_WIN32)
    // Feed the optional -v window only from the real 3DMM movie viewport.
    // In the BRender 1.4 build, -v is the direct OpenGL frontbuffer and must
    // not be overwritten by the old GDI mirror after the GPU swap.
#if defined(BRENDER_MODERN_14)
    if (!FBrModernViewportReady())
#endif
    {
        // BWLD is also used by small secondary BRender previews, such as the
        // 3D text editor. Filter by the canonical movie viewport size.
        if (_rcView.Dxp() == 544 && _rcView.Dyp() == 306 && rc.Dxp() == 544 && rc.Dyp() == 306)
            AppMirrorViewportFromGnv(&gnvTemp, &_rcBuffer, &rc);
    }
#endif // KAUAI_WIN32
}

/** 3DMMv1.0: *************************************************************************
    Add an actor to the world
***************************************************************************/
void BWLD::AddActor(BACT *pbact)
{
    AssertThis(0);
    AssertVarMem(pbact);

    MODERN_BR_BWLD_LOG("BWLD::AddActor BEGIN world=%p actor=%p type=%u model=%p material=%p parent=%p",
                       &_bactWorld, pbact, (unsigned)pbact->type, pbact->model, pbact->material, pbact->parent);
    BrActorAdd(&_bactWorld, pbact);
    MODERN_BR_BWLD_LOG("BWLD::AddActor returned actor=%p parent=%p world_children=%p",
                       pbact, pbact->parent, _bactWorld.children);
}

/** 3DMMv1.0: *************************************************************************
    Filter callback proc for FClickedActor().  Saves pbact if it's the
    closest one hit so far.
***************************************************************************/
int BWLD::_FFilter(BACT *pbact, PBMDL pbmdl, PBMTL pbmtl, BVEC3 *pbvec3RayPos, BVEC3 *pbvec3RayDir, BRS dzpNear,
                   BRS dzpFar, void *pvData)
{
    AssertVarMem(pbact);
    AssertVarMem(pbvec3RayPos);
    AssertVarMem(pbvec3RayDir);

    PBWLD pbwld = (PBWLD)pvData;
    AssertPo(pbwld, 0);

    if (dzpNear < pbwld->_dzpClosestClicked)
    {
        pbwld->_pbactClosestClicked = pbact;
        pbwld->_dzpClosestClicked = dzpNear;
    }

    return fFalse; // 3DMMv1.0: fFalse means keep searching
}

/** 3DMMv1.0: *************************************************************************
    Call pfnCallback for each actor under the point (xp, yp)
***************************************************************************/
void BWLD::IterateActorsInPt(br_pick2d_cbfn *pfnCallback, void *pvArg, int32_t xp, int32_t yp)
{
    AssertThis(0);

    // Keep the main-app picker behavior unchanged while diagnosing the user's
    // report that click-to-select became less forgiving after the Modern
    // viewport work. The v147 and current SCEN/BODY/BWLD pick call chain is
    // source-identical, so record the actual input-to-BRender coordinate
    // contract before replacing it with a rendered-pixel/ID picker.
    const int32_t xpInput = xp;
    const int32_t ypInput = yp;

    // 3DMMv1.0: Convert to _rcBuffer coordinates:
    if (_fHalfX)
        xp /= 2;
    if (_fHalfY)
        yp /= 2;
    xp -= _bpmpRGB.origin_x;
    yp -= _bpmpRGB.origin_y;

    MODERN_BR_BWLD_LOG(
        "main_pick coords input=%ld,%ld pick=%ld,%ld half=%d,%d origin=%d,%d pixelmap=%ux%u row=%d view=%ldx%ld buffer=%ldx%ld",
        (long)xpInput, (long)ypInput, (long)xp, (long)yp,
        (int)_fHalfX, (int)_fHalfY, (int)_bpmpRGB.origin_x, (int)_bpmpRGB.origin_y,
        (unsigned)_bpmpRGB.width, (unsigned)_bpmpRGB.height, (int)_bpmpRGB.row_bytes,
        (long)_rcView.Dxp(), (long)_rcView.Dyp(),
        (long)_rcBuffer.Dxp(), (long)_rcBuffer.Dyp());

    BrScenePick2D(&_bactWorld, &_bactCamera, &_bpmpRGB, xp, yp, pfnCallback, pvArg);
}

/** 3DMMv1.0: *************************************************************************
   If an actor is under (xp, yp), function returns fTrue and **pbact is the
   actor. If no actor is under (xp, yp), function returns fFalse.
***************************************************************************/
bool BWLD::FClickedActor(int32_t xp, int32_t yp, BACT **ppbact)
{
    AssertThis(0);
    AssertVarMem(ppbact);

    _pbactClosestClicked = pvNil;
    _dzpClosestClicked = BR_SCALAR_MAX;

    IterateActorsInPt(BWLD::_FFilter, this, xp, yp);

    if (pvNil != _pbactClosestClicked)
    {
        *ppbact = _pbactClosestClicked;
        return fTrue;
    }

    // 3DMMv1.0: nothing was clicked
    TrashVar(ppbact);
    return fFalse;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the BWLD.
***************************************************************************/
void BWLD::AssertValid(uint32_t grf)
{
    BWLD_PAR::AssertValid(fobjAllocated);
    AssertPo(_pgptWorking, 0);
    AssertPo(_pgptBackground, 0);
    AssertPo(_pzbmpWorking, 0);
    AssertPo(_pzbmpBackground, 0);
    AssertPo(_pregnDirtyWorking, 0);
    AssertPo(_pregnDirtyScreen, 0);
    AssertNilOrPo(_pcrf, 0);
    if (!_fHalfX && _fHalfY)
        AssertPo(_pgptStretch, 0);
    else
        Assert(pvNil == _pgptStretch, "don't need _pgptStretch!");
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the BWLD
***************************************************************************/
void BWLD::MarkMem(void)
{
    AssertThis(0);
    BWLD_PAR::MarkMem();
    MarkMemObj(_pgptWorking);
    MarkMemObj(_pgptBackground);
    MarkMemObj(_pzbmpWorking);
    MarkMemObj(_pzbmpBackground);
    MarkMemObj(_pregnDirtyWorking);
    MarkMemObj(_pregnDirtyScreen);
    MarkMemObj(_pcrf);
    MarkMemObj(_pgptStretch);
}

/** 3DMMv1.0: ****************************************************************************
    FWriteBmp
        Writes the current rendered buffer out to the given file

    Arguments:
        PFNI pfni -- the name of the file

    Returns: fTrue if the file could be written successfully

************************************************************ PETED ***********/
bool BWLD::FWriteBmp(PFNI pfni)
{
    AssertPo(pfni, 0);

    bool fRet = fFalse;
    PGL pgpt = pvNil;

    if (FTrueColorMode())
    {
        Warn("FWriteBmp is unavailable during -c RGB888 rendering");
        return fFalse;
    }
    RC rc;

    pgpt = GPT::PglclrGetPalette();
    AssertPo(pgpt, 0);
    if (pgpt != pvNil)
    {
        fRet = FWriteBitmap(pfni, _pgptWorking->PrgbLockPixels(&rc), pgpt, _rcBuffer.Dxp(), _rcBuffer.Dyp());
        _pgptWorking->Unlock();
        ReleasePpo(&pgpt);
    }

    return fRet;
}

#endif // 3DMMv1.0: DEBUG
