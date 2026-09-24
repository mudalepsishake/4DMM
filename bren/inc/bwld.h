/** 3DMMv1.0: *************************************************************************

    bwld.h: BRender world class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BWLD

***************************************************************************/
#ifndef BWLD_H
#define BWLD_H

// 3DMMv1.0: Callback function per BACT when it's rendered, passing the 2D bounds
typedef void FNBACTREND(PBACT pbact, RC *prc);
typedef FNBACTREND *PFNBACTREND;

// 3DMMv1.0: Callback function per root BACT when we begin rendering
typedef void FNBEGINREND(PBACT pbact);
typedef FNBEGINREND *PFNBEGINREND;

// 3DMMv1.0: Callback function per root BACT to get the rendered rectangle
typedef void FNGETRECT(PBACT pbact, RC *prc);
typedef FNGETRECT *PFNGETRECT;

/** 3DMMv1.0: **************************************
    The BRender world class
****************************************/
typedef class BWLD *PBWLD;
#define BWLD_PAR BASE
#define kclsBWLD KLCONST4('B', 'W', 'L', 'D')
class BWLD : public BWLD_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    static bool _fBRenderInited; // 3DMMv1.0: Whether BrBegin() has been called
    static bool _fTrueColorMode;   // -c: render BRender worlds into RGB888 buffers
    static bool _fActorLightMode;  // -a: enable lit RGB888 mapped materials for actors/props
    static bool _f3DFixMode;       // -3dfix: experimental Source-BRender projection/depth diagnostics
    RC _rcBuffer;                // 3DMMv1.0: Bounds of the rendering space
    RC _rcView;                  // 3DMMv1.0: Bounds of view
    BACT _bactWorld;             // 3DMMv1.0: The world root actor
    BACT _bactCamera;            // 3DMMv1.0: The camera actor
    BCAM _bcam;                  // 3DMMv1.0: The camera data
    PGPT _pgptBackground;        // 3DMMv1.0: Background RGB bitmap
    PGPT _pgptWorking;           // 3DMMv1.0: RGB working buffer to render into
    PGPT _pgptStretch;           // 3DMMv1.0: Stretched working buffer (if _fhalfY)
    BPMP _bpmpRGB;               // 3DMMv1.0: BRender wrapper around _pgptWorking
    PZBMP _pzbmpBackground;      // 3DMMv1.0: Background Z-buffer
    PZBMP _pzbmpWorking;         // 3DMMv1.0: Working Z-buffer to render into
    BPMP _bpmpZ;                 // 3DMMv1.0: BRender wrapper around _pzbmpWorking
    PREGN _pregnDirtyWorking;    // 3DMMv1.0: Rgn to copy from bkgd to working buffer
    PREGN _pregnDirtyScreen;     // 3DMMv1.0: Rgn to copy from working buffer to screen
    bool _fHalfX;                // 3DMMv1.0: Render at half horizontal resolution
    bool _fHalfY;                // 3DMMv1.0: Render at half vertical resolution
    bool _fWorldChanged;         // 3DMMv1.0: Need to rerender?
    PFNBEGINREND _pfnbeginrend;  // 3DMMv1.0: Callback to each actor before rendering
    PFNBACTREND _pfnbactrend;    // 3DMMv1.0: Callback when an actor is rendered
    PFNGETRECT _pfngetrect;      // 3DMMv1.0: Callback to get an actor's bounding rect
    PBACT _pbactClosestClicked;  // 3DMMv1.0: The closest actor that has been clicked
    BRS _dzpClosestClicked;      // 3DMMv1.0: Distance of the closest clicked actor
    // 3DMMv1.0: Keep reference to last background in case we switch to/from halfmode:
    PCRF _pcrf;
    CTG _ctgRGB;
    CNO _cnoRGB;
    CTG _ctgZ;
    CNO _cnoZ;

  protected:
    BWLD(void)
    {
    }
    bool _FInit(int32_t dxp, int32_t dyp, bool fHalfX, bool fHalfY);
    bool _FInitBuffers(int32_t dxp, int32_t dyp, bool fHalfX, bool fHalfY);
    void _CleanWorkingBuffers(void);
    static int BR_CALLBACK _FFilter(BACT *pbact, PBMDL pbmdl, PBMTL pbmtl, BVEC3 *pbvec3RayPos, BVEC3 *pbvec3RayDir,
                                    BRS dzpNear, BRS dzpFar, void *pbwld);

    static void BR_CALLBACK _ActorRendered(PBACT pbact, PBMDL pbmdl, PBMTL pbmtl, br_uint_8 bStyle,
                                           br_matrix4 *pbmat4ModelToScreen, br_int_32 bounds[4]);
    // 3DMMEx: Adapter for new function signature for the br_renderbounds_cbfn callback in BRender v1.3.2
    static void BR_CALLBACK _ActorRenderedNew(PBACT pbact, PBMDL pbmdl, PBMTL pbmtl, void *render_data,
                                              br_uint_8 bStyle, br_matrix4 *pbmat4ModelToScreen, br_int_32 bounds[4]);

  public:
    // 3DMMv1.0: Constructors and destructors
    static PBWLD PbwldNew(int32_t dxp, int32_t dyp, bool fHalfX = fFalse, bool fhalfY = fFalse);
    ~BWLD();
    static void CloseBRender(void);
    static void SetTrueColorMode(bool fEnable);
    static bool FTrueColorMode(void)
    {
        return FPure(_fTrueColorMode);
    }
    static void SetActorLightMode(bool fEnable)
    {
        _fActorLightMode = FPure(fEnable);
    }
    static bool FActorLightMode(void)
    {
        return FPure(_fActorLightMode);
    }
    static void Set3DFixMode(bool fEnable);
    static bool F3DFixMode(void)
    {
        return FPure(_f3DFixMode);
    }

    // 3DMMv1.0: Dirtying the BRender world and bitmap
    void MarkDirty(void)
    {
        _fWorldChanged = fTrue;
    }
    void MarkRenderedRegn(PGOB pgob, int32_t dxp, int32_t dyp);

    // 3DMMv1.0: Background stuff
    bool FSetBackground(PCRF pcrf, CTG ctgRGB, CNO cnoRGB, CTG ctgZ, CNO cnoZ);
    void SetCamera(BMAT34 *pbmat34, BRS zrHither, BRS zrYon, BRA aFov);
    void GetCamera(BMAT34 *pbmat34, BRS *pzrHither = pvNil, BRS *pzrYon = pvNil, BRA *paFov = pvNil);

    // 3DMMv1.0: Actor stuff
    void AddActor(BACT *pbact);
    bool FClickedActor(int32_t xp, int32_t yp, BACT **ppbact);
    void IterateActorsInPt(br_pick2d_cbfn *pfnCallback, void *pvArg, int32_t xp, int32_t yp);
    void SetBeginRenderCallback(PFNBEGINREND pfnbeginrend)
    {
        _pfnbeginrend = pfnbeginrend;
    }
    void SetActorRenderedCallback(PFNBACTREND pfnbactrend)
    {
        _pfnbactrend = pfnbactrend;
    }
    void SetGetRcCallback(PFNGETRECT pfngetrect)
    {
        _pfngetrect = pfngetrect;
    }

    // 3DMMv1.0: Rendering stuff
    bool FSetHalfMode(bool fHalfX, bool fHalfY);
    bool FHalfX(void)
    {
        return _fHalfX;
    }
    bool FHalfY(void)
    {
        return _fHalfY;
    }
    void Render(void);
    void Prerender(void);
    void Unprerender(void);
    void Draw(PGNV pgnv, RC *prcClip, int32_t dxp, int32_t dyp);

#ifdef DEBUG
    bool FWriteBmp(PFNI pfni);
#endif // 3DMMv1.0: DEBUG
};

#endif // 3DMMEx: BWLD_H
