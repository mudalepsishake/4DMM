# actorlight39: experimental -3dfix projection/depth diagnostic.
# Normal rendering is untouched when vg3DFixBRenderMode is zero.
# Under -3dfix, force fully-on-screen models through BRender's generic
# transform/project/clip path and profile SafeFixedMac2Div depth-gradient
# overflow, which otherwise silently returns a zero gradient.

if(NOT DEFINED BRENDER_SOURCE_DIR)
    message(FATAL_ERROR "actorlight39: BRENDER_SOURCE_DIR is required")
endif()

set(_safediv_c "${BRENDER_SOURCE_DIR}/ZB/safediv.c")
set(_awtmz_c "${BRENDER_SOURCE_DIR}/ZB/awtmz.c")
set(_tt24_c "${BRENDER_SOURCE_DIR}/ZB/tt24_piz.c")
set(_zbmesh_c "${BRENDER_SOURCE_DIR}/ZB/zbmesh.c")

foreach(_f IN ITEMS "${_safediv_c}" "${_awtmz_c}" "${_tt24_c}" "${_zbmesh_c}")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "actorlight39: cannot find ${_f}")
    endif()
endforeach()

# -------------------------------------------------------------------------
# safediv.c: count every SafeFixedMac2Div overflow and provide a depth-only
# variant so the raster setup can distinguish Z-gradient failures from UV/
# colour parameter failures. Behaviour remains the original return-zero path.
# -------------------------------------------------------------------------
file(READ "${_safediv_c}" _src)
set(_marker "actorlight39: SafeFixedMac2Div overflow probe")
string(FIND "${_src}" "${_marker}" _already)
if(_already EQUAL -1)
    set(_inc_old [[#include "zb.h"
]])
    set(_inc_new [[#include "zb.h"

/* actorlight39: SafeFixedMac2Div overflow probe */
extern int vgLightingPerformanceEnabled;
extern void RecordPerformanceSafeDivOverflow(void);
extern void RecordPerformanceDepthGradient(int fOverflow);
]])
    string(FIND "${_src}" "${_inc_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "actorlight39: safediv include anchor not found")
    endif()
    string(REPLACE "${_inc_old}" "${_inc_new}" _src "${_src}")

    set(_old [[int BR_ASM_CALL SafeFixedMac2Div(int a, int b, int c, int d, int e)
{
    br_int_64 ab = (br_int_64)a * (br_int_64)b;
    br_int_64 cd = (br_int_64)c * (br_int_64)d;
    return __SafeDiv(ab + cd, e);
}
]])
    set(_new [[static inline int __SafeDivWouldOverflow4DMM(br_int_64 a, br_int_32 b)
{
    if (b == 0)
        return 1;
    if (a < 0)
        a = -a;
    if (b < 0)
        b = -b;
    return (a >> 32) >= b;
}

int BR_ASM_CALL SafeFixedMac2Div(int a, int b, int c, int d, int e)
{
    br_int_64 ab = (br_int_64)a * (br_int_64)b;
    br_int_64 cd = (br_int_64)c * (br_int_64)d;
    br_int_64 sum = ab + cd;
    if (vgLightingPerformanceEnabled && __SafeDivWouldOverflow4DMM(sum, e))
        RecordPerformanceSafeDivOverflow();
    return __SafeDiv(sum, e);
}

int BR_ASM_CALL SafeFixedMac2DivDepth4DMM(int a, int b, int c, int d, int e)
{
    br_int_64 ab = (br_int_64)a * (br_int_64)b;
    br_int_64 cd = (br_int_64)c * (br_int_64)d;
    br_int_64 sum = ab + cd;
    if (vgLightingPerformanceEnabled)
    {
        int fOverflow = __SafeDivWouldOverflow4DMM(sum, e);
        RecordPerformanceDepthGradient(fOverflow);
        if (fOverflow)
            RecordPerformanceSafeDivOverflow();
    }
    return __SafeDiv(sum, e);
}
]])
    string(FIND "${_src}" "${_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "actorlight39: SafeFixedMac2Div body anchor not found")
    endif()
    string(REPLACE "${_old}" "${_new}" _src "${_src}")
    file(WRITE "${_safediv_c}" "${_src}")
    message(STATUS "actorlight39: instrumented ZB/safediv.c")
else()
    message(STATUS "actorlight39: ZB/safediv.c already instrumented")
endif()

# -------------------------------------------------------------------------
# awtmz.c: arbitrary-width textured RGB888 path. Replace only the single Z
# setup invocation with an explicit depth-probed equivalent; UV and lighting
# parameters continue through the original PARAM_SETUP macro unchanged.
# -------------------------------------------------------------------------
file(READ "${_awtmz_c}" _src)
set(_marker "actorlight39: depth-specific parameter setup")
string(FIND "${_src}" "${_marker}" _already)
if(_already EQUAL -1)
    set(_decl_old "extern int BR_ASM_CALL SafeFixedMac2Div(int, int, int, int, int);")
    set(_decl_new [[extern int BR_ASM_CALL SafeFixedMac2Div(int, int, int, int, int);
extern int BR_ASM_CALL SafeFixedMac2DivDepth4DMM(int, int, int, int, int);]])
    string(FIND "${_src}" "${_decl_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "actorlight39: awtmz SafeFixedMac2Div declaration not found")
    endif()
    string(REPLACE "${_decl_old}" "${_decl_new}" _src "${_src}")

    set(_z_old "        PARAM_SETUP(zb.pz, v[Z]);")
    set(_z_new [[        /* actorlight39: depth-specific parameter setup */
        dp1 = b->v[Z] - a->v[Z];
        dp2 = c->v[Z] - a->v[Z];
        zb.pz.grad_x = SafeFixedMac2DivDepth4DMM(dp1, zb.main.y, dp2, -zb.top.y, g_divisor);
        zb.pz.grad_y = SafeFixedMac2DivDepth4DMM(dp2, zb.top.x, dp1, -zb.main.x, g_divisor);
        zb.pz.current = a->v[Z] + zb.pz.grad_x / 2;
        zb.pz.d_nocarry = BrFixedToInt(zb.main.grad) * zb.pz.grad_x + zb.pz.grad_y;
        zb.pz.d_carry = zb.pz.d_nocarry + zb.pz.grad_x;]])
    string(FIND "${_src}" "${_z_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "actorlight39: awtmz Z parameter setup not found")
    endif()
    string(REPLACE "${_z_old}" "${_z_new}" _src "${_src}")
    file(WRITE "${_awtmz_c}" "${_src}")
    message(STATUS "actorlight39: instrumented textured Z gradients")
else()
    message(STATUS "actorlight39: ZB/awtmz.c already instrumented")
endif()

# -------------------------------------------------------------------------
# tt24_piz.c: RGB888 solid/Gouraud path. Replace only the two Z helper calls
# with explicit depth-probed setup; RGB parameters retain legacy setup.
# -------------------------------------------------------------------------
file(READ "${_tt24_c}" _src)
set(_marker "actorlight39: solid depth-specific parameter setup")
string(FIND "${_src}" "${_marker}" _already)
if(_already EQUAL -1)
    set(_decl_old "extern int BR_ASM_CALL SafeFixedMac2Div(int, int, int, int, int);")
    set(_decl_new [[extern int BR_ASM_CALL SafeFixedMac2Div(int, int, int, int, int);
extern int BR_ASM_CALL SafeFixedMac2DivDepth4DMM(int, int, int, int, int);]])
    string(FIND "${_src}" "${_decl_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "actorlight39: tt24 SafeFixedMac2Div declaration not found")
    endif()
    string(REPLACE "${_decl_old}" "${_decl_new}" _src "${_src}")

    set(_z_old "    __PARAM_PI_DIRN(a->v[2], b->v[2], c->v[2], &zb.pz);")
    string(REGEX MATCHALL "__PARAM_PI_DIRN\\(a->v\\[2\\], b->v\\[2\\], c->v\\[2\\], &zb\\.pz\\)" _matches "${_src}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL 2)
        message(FATAL_ERROR "actorlight39: expected 2 tt24 Z setup sites, found ${_count}")
    endif()
    set(_z_new [[    /* actorlight39: solid depth-specific parameter setup */
    zb.pz.grad_x = SafeFixedMac2DivDepth4DMM(b->v[2] - a->v[2], zb.main.y,
                                              c->v[2] - a->v[2], -zb.top.y, g_divisor);
    zb.pz.grad_y = SafeFixedMac2DivDepth4DMM(c->v[2] - a->v[2], zb.top.x,
                                              b->v[2] - a->v[2], -zb.main.x, g_divisor);
    zb.pz.current = a->v[2] + zb.pz.grad_x / 2;
    zb.pz.d_nocarry = BrFixedToInt(zb.main.grad) * zb.pz.grad_x + zb.pz.grad_y;
    zb.pz.d_carry = zb.pz.d_nocarry + zb.pz.grad_x;]])
    string(REPLACE "${_z_old}" "${_z_new}" _src "${_src}")
    file(WRITE "${_tt24_c}" "${_src}")
    message(STATUS "actorlight39: instrumented solid RGB888 Z gradients")
else()
    message(STATUS "actorlight39: ZB/tt24_piz.c already instrumented")
endif()

# -------------------------------------------------------------------------
# zbmesh.c: under -3dfix only, turn OSC_ACCEPT into OSC_PARTIAL before the
# FAST_PROJECT branch. This forces the generic transform/project/outcode path
# and gives us a clean A/B against fixed-build on-screen projection shortcuts.
# -------------------------------------------------------------------------
file(READ "${_zbmesh_c}" _src)
set(_marker "actorlight39: -3dfix generic projection probe")
string(FIND "${_src}" "${_marker}" _already)
if(_already EQUAL -1)
    set(_inc_anchor [[extern int vgLightingPerformanceEnabled;
extern void RecordPerformanceModelRelit(void);
]])
    set(_inc_new [[extern int vgLightingPerformanceEnabled;
extern void RecordPerformanceModelRelit(void);
/* actorlight39: -3dfix generic projection probe */
extern int vg3DFixBRenderMode;
extern void RecordPerformance3DFixMesh(int fAcceptInput, int fForcedPartial);
]])
    string(FIND "${_src}" "${_inc_anchor}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "actorlight39: expected logperf17 zbmesh declarations not found")
    endif()
    string(REPLACE "${_inc_anchor}" "${_inc_new}" _src "${_src}")

    set(_fast_anchor [[#if FAST_PROJECT
    /*
     * If on_screen, then let the fast project code pre process the model->screen
]])
    set(_fast_new [[    if (vgLightingPerformanceEnabled)
        RecordPerformance3DFixMesh(on_screen == OSC_ACCEPT,
                                   vg3DFixBRenderMode && on_screen == OSC_ACCEPT);
    if (vg3DFixBRenderMode && on_screen == OSC_ACCEPT)
        on_screen = OSC_PARTIAL;

#if FAST_PROJECT
    /*
     * If on_screen, then let the fast project code pre process the model->screen
]])
    string(FIND "${_src}" "${_fast_anchor}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "actorlight39: zbmesh FAST_PROJECT anchor not found")
    endif()
    string(REPLACE "${_fast_anchor}" "${_fast_new}" _src "${_src}")
    file(WRITE "${_zbmesh_c}" "${_src}")
    message(STATUS "actorlight39: added runtime generic-projection probe to ZB/zbmesh.c")
else()
    message(STATUS "actorlight39: ZB/zbmesh.c already patched")
endif()
