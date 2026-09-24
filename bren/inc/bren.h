/** 3DMMv1.0: *************************************************************************

    bren.h: Main include file for BRender files

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#ifndef BREN_H
#define BREN_H

#include "kidframe.h"
// Modern BRender's compiler.h defines DEBUG to 0 in non-debug builds.
// Kauai/3DMM uses #ifdef DEBUG, so letting that macro escape BRender makes
// release translation units compile debug-only method bodies whose class
// declarations were omitted earlier. Preserve the application's DEBUG state
// across the BRender public header.
#if defined(DEBUG)
#define THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER 1
#endif
#include "brender.h"
#if defined(BRENDER_MODERN_14) && !defined(THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER) && defined(DEBUG)
#undef DEBUG
#endif
#ifdef THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER
#undef THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER
#endif

#if defined(BRENDER_MODERN_14)
// Experimental BRender 1.4/OpenGL bridge used by the Win32 -v viewport.
// Implemented in the Studio executable so BRender's static begin hook can
// register glrend without introducing a core/driver circular dependency.
void BrModernLogBootstrapFromCommandLine(void);
void BrModernLogSetEnabled(bool fEnable);
bool FBrModernLogEnabled(void);
void BrModernLog(const char *pszFormat, ...);
void BrModernLogException(void *pvExceptionPointers, const char *pszWhere);
void BrShadowLogSetEnabled(bool fEnable);
bool FBrShadowLogEnabled(void);
void BrShadowLog(const char *pszFormat, ...);
void BrModernInstallDiagHandler(void);

void BrModernViewportSetWindow(void *pvHwnd);
void BrModernViewportConfigure(int32_t dxp, int32_t dyp, bool fExternalOnly);
bool FBrModernViewportReady(void);
bool FBrModernViewportRender(void *pvOwner, struct br_actor *pbactWorld, struct br_actor *pbactCamera,
                             struct br_pixelmap *pbpmpCpu, bool fPresent);
void BrModernViewportPresent(void);
bool FBrModernViewportPaintLatestFrame(void *pvHdc, int32_t xpDst, int32_t ypDst,
                                       int32_t dxpDst, int32_t dypDst);
void BrModernViewportClearCachedFrame(void);
// APE-backed editor previews (3D Word, Costume Changer, Action) identify their
// BWLD and source-client rectangle here. In scaled Modern mode the BWLD is
// rendered at presentation resolution into a dedicated CPU cache. The visible
// 4DMM presentation paints that cache itself; Kauai's DWM UI layer is cut
// around the same rectangle so no child/top-level preview HWND participates in
// Z-order. Menus/tooltips remain ordinary Kauai UI above the native preview.
void BrModernPreviewActivate(void *pvOwner, int32_t xpSource, int32_t ypSource,
                             int32_t dxpSource, int32_t dypSource,
                             int32_t xpOwnerSource, int32_t ypOwnerSource,
                             int32_t dxpOwnerSource, int32_t dypOwnerSource);
void BrModernPreviewDeactivate(void *pvOwner);
bool FBrModernPreviewGetSourceRect(int32_t *pxpSource, int32_t *pypSource,
                                   int32_t *pdxpSource, int32_t *pdypSource);
// Active preview geometry is valid before the first native readback frame.
bool FBrModernPreviewGetActiveSourceRect(int32_t *pxpSource, int32_t *pypSource,
                                         int32_t *pdxpSource, int32_t *pdypSource);
bool FBrModernPreviewGetOwnerSourceRect(int32_t *pxpSource, int32_t *pypSource,
                                        int32_t *pdxpSource, int32_t *pdypSource);
bool FBrModernPreviewPaintLatestFrame(void *pvHdc, int32_t xpDst, int32_t ypDst,
                                      int32_t dxpDst, int32_t dypDst);
void BrModernViewportShutdown(void);
#endif

typedef br_actor BACT;
typedef br_model BMDL;
typedef br_light BLIT;
typedef br_camera BCAM;
typedef br_material BMTL;
typedef br_pixelmap BPMP;
typedef br_matrix34 BMAT34;
typedef br_bounds BRB;
typedef br_vertex BRV;
typedef br_face BRFC;

/* 3DMMEx: On-disk representation of BRF */
struct br_face_file
{
    br_uint_16 vertices[3]; /* 3DMMEx: Vertices around face 				*/
    br_uint_16 edges[3];    /* 3DMMEx: Edges around face					*/
    br_uint_32 material;    /* 3DMMEx: was: Face material (or NULL) 		*/
    br_uint_16 smoothing;   /* 3DMMEx: Controls if shared edges are smooth	*/
    br_uint_8 flags;        /* 3DMMEx: Bits 0,1 and 2 denote internal edges	*/
    br_uint_8 _pad0;
    br_fvector3 n; /* 3DMMEx: Plane equation of face				*/
    br_scalar d;
};
VERIFY_STRUCT_SIZE(br_face_file, 32)

typedef br_face_file BRFF;
typedef br_face BRF;
typedef br_colour BRCLR;
typedef br_transform BRXFM;
typedef br_euler BREUL;
typedef br_matrix4 BMAT4;
typedef br_matrix23 BMAT23;

typedef BACT *PBACT;
typedef BMDL *PBMDL;
typedef BLIT *PBLIT;
typedef BCAM *PBCAM;
typedef BMTL *PBMTL;
typedef BPMP *PBPMP;
typedef BMAT34 *PBMAT34;
typedef BMAT4 *PBMAT4;
typedef BMAT23 *PBMAT23;

typedef br_scalar BRS;
typedef br_angle BRA;
typedef br_fraction BRFR;
typedef br_ufraction BRUFR;
typedef br_vector3 BVEC3;
typedef br_vector4 BVEC4;

#define BrsMac2 BR_MAC2
#define BrsSub BR_SUB
#define BrsMul BR_MUL
#define BrsAdd BR_ADD
#define BrsAbs BR_ABS
#define BrsRcp BR_RCP

#if defined(BRENDER_MODERN_14) && !defined(BR_MPREP_ALL)
#define BR_MPREP_ALL BR_MODU_ALL
#endif

const BRS rZero = BR_SCALAR(0.0);
const BRS rOne = BR_SCALAR(1.0);
const BRS rTwo = BR_SCALAR(2.0);
const BRS rFour = BR_SCALAR(4.0);
const BRS rOneHalf = BR_SCALAR(0.5);
const BRS rEps = BR_SCALAR_EPSILON;
const BRS rDivMin = 0x03; // 3DMMv1.0: Smallest br_scalar for which BrsRcp() does not overflow
const BRS rFractMax = rOne - rEps;

const BRS krQuarter = BR_SCALAR(0.25);
const BRS krHalf = BR_SCALAR(0.5);
const BRS krPi = BR_SCALAR(PI);
const BRS krTwoPi = BR_SCALAR(2.0 * PI);
const BRS krThreePi = BR_SCALAR(3.0 * PI);
const BRS krHalfPi = BR_SCALAR(0.5 * PI);

const BVEC3 vec3X = {rOne, rZero, rZero};
const BVEC3 vec3Y = {rZero, rOne, rZero};
const BVEC3 vec3Z = {rZero, rZero, rOne};

const BRFR frNil = BR_FRACTION(0.0);
const BRA aNil = BR_ANGLE_DEG(0);
const BRA aZero = BR_ANGLE_DEG(0);

#define kctgZbmp KLCONST4('Z', 'B', 'M', 'P')

struct BCB // 3DMMv1.0: bounding cuboid...same shape as br_bounds
{
    BRS xrMin;
    BRS yrMin;
    BRS zrMin;
    BRS xrMax;
    BRS yrMax;
    BRS zrMax;
};
const BOM kbomBcb = 0xfff00000;

inline bool FBrEmptyBcb(BCB *pbcb)
{
    return !(pbcb->xrMin || pbcb->yrMin || pbcb->zrMin || pbcb->xrMax || pbcb->yrMax || pbcb->zrMax);
}

const BOM kbomBrs = 0xc0000000; // 3DMMv1.0: br_scalar
const BOM kbomBrv = 0xffd50000; // 3DMMv1.0: br_vertex
const BOM kbomBrf = 0x555c15c0; // 3DMMv1.0: br_face
const BOM kbomBmat34 = 0xffffff00;

// 3DMMEx: Check sizes of BRender structures used in the file format
#if BASED_FIXED
VERIFY_STRUCT_SIZE(BRA, 2);
VERIFY_STRUCT_SIZE(BRB, 24);
VERIFY_STRUCT_SIZE(BMAT34, 48);
VERIFY_STRUCT_SIZE(BRS, 4);
VERIFY_STRUCT_SIZE(BRV, 32);
VERIFY_STRUCT_SIZE(BVEC3, 12);
VERIFY_STRUCT_SIZE(br_colour, 4);
VERIFY_STRUCT_SIZE(br_ufraction, 2);
#else // 3DMMEx: !BASED_FIXED
#error BRender must be configured for fixed-point math
#endif // 3DMMv1.0: BASED_FIXED

#if BASED_FIXED
inline BRS BrsHalf(BRS r)
{
    return r >> 1;
}
inline BRFR ScalarToFraction(BRS r)
{
    return (br_fraction)((r >> 1) | ((r & 0x8000) >> 8));
}
inline BRS BrsAbsMax3(BRS r1, BRS r2, BRS r3)
{
    return (BRS)(LwMax(LwMax(LwAbs((long)r1), LwAbs((long)r2)), LwAbs((long)r3)));
}
inline BRS BrsDiv(BRS r1, BRS r2) // 3DMMv1.0: Safety net: Prevent ovfl on division of integers
{
    if (r2 >= rOne || r2 < -rOne || LwAbs((long)r1) < LwAbs((long)r2))
        return BR_DIV(r1, r2); // 3DMMv1.0: Most common case in SocRates (by far)
    if (LwAbs((long)r2) < rDivMin)
        return ((r1 > 0 == r2 > 0) ? BR_SCALAR_MAX : BR_SCALAR_MIN);
    BRS rRcp = BR_RCP(r2);
    uint32_t lwT = (((uint32_t)BR_ABS(r1)) >> 16) * (((uint32_t)BR_ABS(rRcp)) >> 16);
    if (lwT < 65536)
        return (BRS)BR_MUL(r1, rRcp);
    return ((r1 > 0 == r2 > 0) ? BR_SCALAR_MAX : BR_SCALAR_MIN);
}
#endif // 3DMMv1.0: BASED_FIXED

// 3DMMv1.0: fixed.h additions

// 3DMMv1.0: Round by adding half the desired rounding precision, and masking off the
// 3DMMv1.0: remaining precision
#define BR_ROUND(s, p) ((br_scalar)(BR_ADD((s), (0x01 << (p))) & ~((br_scalar)((0x01 << ((p) + 1)) - 1))))

// 3DMMv1.0: colour.h additions

// 3DMMv1.0: Color conversion: straight RGB to RGB and RGB to grey
#define BR_DK_TO_BR(dk) BR_COLOUR_RGB((uint8_t)((dk).r * 256.0), (uint8_t)((dk).g * 256.0), (uint8_t)((dk).b * 256.0))
#define BR_DKRGB_TO_FRGRAY(dk) BrScalarToUFraction(BrFloatToScalar(((dk).r * 0.30 + (dk).g * 0.59 + (dk).b * 0.11)))

// 3DMMv1.0: angles.h additions
// 3DMMv1.0: REVIEW peted: temporary BR_ACOS until we get a fixed Brender
#ifdef BR_ACOS
#undef BR_ACOS
#endif // 3DMMv1.0: BR_ACOS
#define BR_ACOS(a) BrRadianToAngle(BrFloatToScalar(acos(BrScalarToFloat(a))))

#include "zbmp.h"
#include "bwld.h"
#include "tmap.h"

#endif //! 3DMMv1.0: BREN_H
