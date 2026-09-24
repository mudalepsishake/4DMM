# actorlight36: keep actorlight32's full 16.16 projected-Z sidecar and v34's
# RGB888 raster safety guards, but shrink v35's coplanar ownership window to
# one-sixteenth of one legacy 16-bit Z step.  v35 removed Venetian blinds but
# could still swallow real front/back separation when the camera was far away.

if(NOT DEFINED BRENDER_SOURCE_DIR)
    message(FATAL_ERROR "actorlight32: BRENDER_SOURCE_DIR is required")
endif()

set(_zb_h "${BRENDER_SOURCE_DIR}/ZB/zb.h")
set(_zbrendr_c "${BRENDER_SOURCE_DIR}/ZB/zbrendr.c")
set(_tt24_c "${BRENDER_SOURCE_DIR}/ZB/tt24_piz.c")
set(_tpiza_c "${BRENDER_SOURCE_DIR}/ZB/t_piza.c")

foreach(_f IN ITEMS "${_zb_h}" "${_zbrendr_c}" "${_tt24_c}" "${_tpiza_c}")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "actorlight32: cannot find ${_f}")
    endif()
endforeach()

# -------------------------------------------------------------------------
# ZB/zb.h: add a full-precision sidecar pointer and one common depth helper.
# -------------------------------------------------------------------------
file(READ "${_zb_h}" _src)
set(_marker "actorlight32: full 16.16 depth sidecar")
string(FIND "${_src}" "${_marker}" _already)
if(_already EQUAL -1)
    set(_old [[        br_uint_8 *colour_buffer;  /* Colour buffer		*/
        br_fixed_ls *depth_buffer; /* Z buffer				*/
        br_int_32 row_width;       /* Stride (in bytes)	*/]])
    set(_new [[        br_uint_8 *colour_buffer;  /* Colour buffer		*/
        br_fixed_ls *depth_buffer; /* Z buffer				*/
        // actorlight32: full 16.16 depth sidecar. The legacy 16-bit depth
        // buffer remains authoritative for 3DMM background/ZBMP compatibility.
        br_uint_32 *depth_buffer_full;
        br_int_32 row_width;       /* Stride (in bytes)	*/]])
    string(FIND "${_src}" "${_old}" _found)
    if(_found EQUAL -1)
        # Ben Stone's formatting has changed slightly across revisions; use a
        # whitespace-tolerant insertion immediately after depth_buffer.
        string(REGEX REPLACE
            "(br_fixed_ls[ \t]+\\*depth_buffer;[^\n]*\n)"
            "\\1        // actorlight32: full 16.16 depth sidecar. The legacy 16-bit depth\\n        // buffer remains authoritative for 3DMM background/ZBMP compatibility.\\n        br_uint_32 *depth_buffer_full;\\n"
            _src "${_src}")
    else()
        string(REPLACE "${_old}" "${_new}" _src "${_src}")
    endif()

    set(_extern_old "    extern br_zbuffer_state BR_ASM_DATA zb;")
    set(_helper [[    extern br_zbuffer_state BR_ASM_DATA zb;

    // actorlight36: preserve full 16.16 ordering and bridge only a tiny
    // fraction of a legacy Z bucket.  This retains v33/v35's stable ownership
    // for genuinely near-coplanar surfaces without letting one whole old Z
    // step erase real depth separation as camera distance increases.
    #define FOURDMM_Z_COPLANAR_EPSILON (1u << 12)
    static inline br_boolean ZbDepthTestWrite4DMM(br_uint_16 *zptr, br_uint_32 zvalue,
                                                   br_boolean legacy_equal_pass)
    {
        if (zb.depth_buffer_full != NULL)
        {
            br_int_32 iz = (br_int_32)(zptr - (br_uint_16 *)zb.depth_buffer);
            br_int_32 zstride = zb.depth_row_width / (br_int_32)sizeof(br_uint_16);
            br_int_32 yp;
            br_int_32 xp;
            br_int_32 ipixel;
            br_uint_32 *zfull;
            br_uint_32 zold;
            br_uint_16 znew16;

            if (fw.output == NULL || zstride <= 0 || iz < 0)
                return BR_FALSE;
            yp = iz / zstride;
            xp = iz - yp * zstride;
            if (xp < 0 || xp >= fw.output->width || yp < 0 || yp >= fw.output->height)
                return BR_FALSE;

            ipixel = yp * fw.output->width + xp;
            zfull = zb.depth_buffer_full + ipixel;
            zold = *zfull;
            znew16 = (br_uint_16)BrFixedToInt(zvalue);

            if (zold < zvalue)
            {
                br_uint_32 dz = zvalue - zold;
                if (dz > FOURDMM_Z_COPLANAR_EPSILON)
                    return BR_FALSE;

                // Near-coplanar but fractionally farther.  Let this later
                // surface own colour without moving the stored nearest depth.
                return BR_TRUE;
            }

            *zfull = zvalue;
            *zptr = znew16;
            return BR_TRUE;
        }

        br_uint_16 z16 = (br_uint_16)BrFixedToInt(zvalue);
        if (legacy_equal_pass)
        {
            if (*zptr < z16)
                return BR_FALSE;
        }
        else if (*zptr <= z16)
            return BR_FALSE;

        *zptr = z16;
        return BR_TRUE;
    }

    // actorlight34: giant models can generate scan spans beyond the RGB/Z
    // pixelmaps even when the normal clipper has already run. Reject those
    // pixels before any legacy rasterizer dereferences an invalid pointer.
    static inline br_boolean ZbRasterIndexValid4DMM(br_int_32 ipixel)
    {
        if (fw.output == NULL || zb.row_width <= 0 || fw.output->height <= 0)
            return BR_FALSE;
        return ipixel >= 0 && ipixel < (zb.row_width / 3) * fw.output->height;
    }

    static inline br_boolean ZbRasterPointersValid4DMM(const br_uint_8 *colour_ptr,
                                                        const br_uint_16 *zptr, br_uint_32 cb_pixel)
    {
        const br_uint_8 *colour_first;
        const br_uint_8 *colour_lim;
        const br_uint_8 *depth_first;
        const br_uint_8 *depth_lim;
        const br_uint_8 *depth_ptr;

        if (fw.output == NULL || colour_ptr == NULL || zptr == NULL || cb_pixel == 0 ||
            zb.row_width <= 0 || zb.depth_row_width <= 0 || fw.output->height <= 0)
            return BR_FALSE;

        colour_first = zb.colour_buffer;
        colour_lim = colour_first + zb.row_width * fw.output->height;
        depth_first = (const br_uint_8 *)zb.depth_buffer;
        depth_lim = depth_first + zb.depth_row_width * fw.output->height;
        depth_ptr = (const br_uint_8 *)zptr;

        return colour_ptr >= colour_first && colour_ptr + cb_pixel <= colour_lim &&
               depth_ptr >= depth_first && depth_ptr + sizeof(br_uint_16) <= depth_lim;
    }]])
    string(FIND "${_src}" "${_extern_old}" _found_extern)
    if(_found_extern EQUAL -1)
        message(FATAL_ERROR "actorlight32: zb state extern anchor not found")
    endif()
    string(REPLACE "${_extern_old}" "${_helper}" _src "${_src}")
    file(WRITE "${_zb_h}" "${_src}")
    message(STATUS "actorlight32: added full-depth sidecar state/helper to ZB/zb.h")
else()
    message(STATUS "actorlight32: ZB/zb.h already patched")
endif()

# actorlight36: incremental builds may reuse v34/v35-patched BRender. Replace
# only the depth-policy portion in place and leave the framebuffer guards intact.
file(READ "${_zb_h}" _src)
set(_v35_marker "actorlight35: keep full 16.16 depth")
set(_v36_marker "actorlight36: preserve full 16.16 ordering")
string(FIND "${_src}" "${_v35_marker}" _v35_already)
string(FIND "${_src}" "${_v36_marker}" _v36_preexisting)
if(_v35_already EQUAL -1 AND _v36_preexisting EQUAL -1)
    set(_v34_depth_marker "    // actorlight34: preserve full 16.16 depth for genuinely separated surfaces.\n")
    set(_v34_guard_marker "    // actorlight34: giant models can generate scan spans beyond the RGB/Z\n")
    string(FIND "${_src}" "${_v34_depth_marker}" _depth_begin)
    string(FIND "${_src}" "${_v34_guard_marker}" _guard_begin)
    if(_depth_begin EQUAL -1 OR _guard_begin EQUAL -1 OR _guard_begin LESS_EQUAL _depth_begin)
        message(FATAL_ERROR "actorlight35: expected current v34 depth helper not found; use the v34 source tree or a clean BRender checkout")
    endif()

    string(SUBSTRING "${_src}" 0 ${_depth_begin} _prefix)
    string(LENGTH "${_src}" _src_len)
    math(EXPR _suffix_len "${_src_len} - ${_guard_begin}")
    string(SUBSTRING "${_src}" ${_guard_begin} ${_suffix_len} _suffix)
    set(_v35_depth [[    // actorlight36: preserve full 16.16 ordering and bridge only a tiny
    // fraction of a legacy Z bucket.  This retains v33/v35's stable ownership
    // for genuinely near-coplanar surfaces without letting one whole old Z
    // step erase real depth separation as camera distance increases.
    #define FOURDMM_Z_COPLANAR_EPSILON (1u << 12)
    static inline br_boolean ZbDepthTestWrite4DMM(br_uint_16 *zptr, br_uint_32 zvalue,
                                                   br_boolean legacy_equal_pass)
    {
        if (zb.depth_buffer_full != NULL)
        {
            br_int_32 iz = (br_int_32)(zptr - (br_uint_16 *)zb.depth_buffer);
            br_int_32 zstride = zb.depth_row_width / (br_int_32)sizeof(br_uint_16);
            br_int_32 yp;
            br_int_32 xp;
            br_int_32 ipixel;
            br_uint_32 *zfull;
            br_uint_32 zold;
            br_uint_16 znew16;

            if (fw.output == NULL || zstride <= 0 || iz < 0)
                return BR_FALSE;
            yp = iz / zstride;
            xp = iz - yp * zstride;
            if (xp < 0 || xp >= fw.output->width || yp < 0 || yp >= fw.output->height)
                return BR_FALSE;

            ipixel = yp * fw.output->width + xp;
            zfull = zb.depth_buffer_full + ipixel;
            zold = *zfull;
            znew16 = (br_uint_16)BrFixedToInt(zvalue);

            if (zold < zvalue)
            {
                br_uint_32 dz = zvalue - zold;
                if (dz > FOURDMM_Z_COPLANAR_EPSILON)
                    return BR_FALSE;

                // Near-coplanar but fractionally farther.  Let this later
                // surface own colour without moving the stored nearest depth.
                return BR_TRUE;
            }

            *zfull = zvalue;
            *zptr = znew16;
            return BR_TRUE;
        }

        br_uint_16 z16 = (br_uint_16)BrFixedToInt(zvalue);
        if (legacy_equal_pass)
        {
            if (*zptr < z16)
                return BR_FALSE;
        }
        else if (*zptr <= z16)
            return BR_FALSE;

        *zptr = z16;
        return BR_TRUE;
    }

]])
    set(_src "${_prefix}${_v35_depth}${_suffix}")
    file(WRITE "${_zb_h}" "${_src}")
    message(STATUS "actorlight36: upgraded v34 depth ownership to fractional coplanar tolerance")
elseif(NOT _v36_preexisting EQUAL -1)
    message(STATUS "actorlight36: fractional coplanar depth ownership already present")
else()
    message(STATUS "actorlight35: prior one-step coplanar depth ownership detected")
endif()

# actorlight36: an incremental checkout can already contain the complete v35
# helper, causing the v34->v35 block above to skip.  Upgrade that helper in
# place without requiring deletion of _deps.
file(READ "${_zb_h}" _src)
string(FIND "${_src}" "actorlight36: preserve full 16.16 ordering" _v36_already)
if(_v36_already EQUAL -1)
    string(FIND "${_src}" "#define FOURDMM_Z_COPLANAR_EPSILON (1u << 16)" _old_eps)
    if(_old_eps EQUAL -1)
        message(FATAL_ERROR "actorlight36: existing full-depth helper has an unexpected coplanar epsilon")
    endif()
    string(REPLACE "#define FOURDMM_Z_COPLANAR_EPSILON (1u << 16)"
                   "#define FOURDMM_Z_COPLANAR_EPSILON (1u << 12)" _src "${_src}")
    string(REPLACE
        "    // actorlight35: keep full 16.16 depth for ordinary surfaces, but bridge\n    // the single legacy-Z-step quantization boundary that produced alternating\n    // scanline ownership on nearly coplanar/intersecting faces.  This is 4x\n    // narrower than v33, whose four-step tolerance could swallow legitimate\n    // front/back separation at distance.  Within this one-step band, later\n    // draw order owns colour while the genuinely nearer depth remains stored.\n"
        "    // actorlight36: preserve full 16.16 ordering and bridge only a tiny\n    // fraction of a legacy Z bucket.  This retains v33/v35's stable ownership\n    // for genuinely near-coplanar surfaces without letting one whole old Z\n    // step erase real depth separation as camera distance increases.\n"
        _src "${_src}")
    file(WRITE "${_zb_h}" "${_src}")
    message(STATUS "actorlight36: shrank near-coplanar ownership to 1/16 legacy Z step")
else()
    message(STATUS "actorlight36: fractional near-coplanar depth ownership already applied")
endif()

# -------------------------------------------------------------------------
# ZB/zbrendr.c: tiny setter used by 4DMM immediately around scene rendering.
# -------------------------------------------------------------------------
file(READ "${_zbrendr_c}" _src)
set(_setter_marker "actorlight32: install transient full-depth sidecar")
string(FIND "${_src}" "${_setter_marker}" _already)
if(_already EQUAL -1)
    set(_anchor "void BR_PUBLIC_ENTRY BrZbSceneRenderBegin(br_actor *world, br_actor *camera, br_pixelmap *colour_buffer,")
    set(_insert [[// actorlight32: install transient full-depth sidecar supplied by 4DMM.
// It is deliberately not part of the public 1995 BRender API; the app clears
// it immediately after BrZbSceneRender returns.
void BR_PUBLIC_ENTRY BrZbUseFullDepthBuffer(br_uint_32 *buffer)
{
    zb.depth_buffer_full = buffer;
}

void BR_PUBLIC_ENTRY BrZbSceneRenderBegin(br_actor *world, br_actor *camera, br_pixelmap *colour_buffer,]])
    string(FIND "${_src}" "${_anchor}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "actorlight32: BrZbSceneRenderBegin anchor not found")
    endif()
    string(REPLACE "${_anchor}" "${_insert}" _src "${_src}")
    file(WRITE "${_zbrendr_c}" "${_src}")
    message(STATUS "actorlight32: added BrZbUseFullDepthBuffer")
else()
    message(STATUS "actorlight32: ZB/zbrendr.c already patched")
endif()

# -------------------------------------------------------------------------
# ZB/t_piza.c: arbitrary-width textured triangles (including RGB888 mapped
# actors/props). Replace both forward/backward legacy comparisons without
# depending on surrounding whitespace or CRLF layout.
# -------------------------------------------------------------------------
file(READ "${_tpiza_c}" _src)
set(_tpiza_marker "ZbDepthTestWrite4DMM(zptr")
string(FIND "${_src}" "${_tpiza_marker}" _already)
if(_already EQUAL -1)
    set(_ztest_old "if (*zptr < BrFixedToInt(zb.pz.currentpix))")
    set(_ztest_new "if (!ZbDepthTestWrite4DMM(zptr, (br_uint_32)zb.pz.currentpix, BR_TRUE))")
    string(REGEX MATCHALL "if \\(\\*zptr < BrFixedToInt\\(zb\\.pz\\.currentpix\\)\\)" _matches "${_src}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL 2)
        message(FATAL_ERROR "actorlight33hf1: expected 2 t_piza legacy Z-test sites, found ${_count}")
    endif()
    string(REPLACE "${_ztest_old}" "${_ztest_new}" _src "${_src}")

    # The helper owns the accepted depth write for both traversal directions.
    # Remove every legacy write associated with those branches.  Do NOT count
    # these with REGEX MATCHALL + list(LENGTH): the matched C statement ends in
    # a semicolon, and CMake treats that semicolon as a list separator, making
    # the three real matches appear as six list elements under CMP0007 NEW.
    set(_zwrite_old "*zptr = BrFixedToInt(zb.pz.currentpix);")
    string(FIND "${_src}" "${_zwrite_old}" _write_found)
    if(_write_found EQUAL -1)
        message(FATAL_ERROR "actorlight33hf2: no legacy t_piza Z writes found after replacing Z tests")
    endif()
    string(REPLACE "${_zwrite_old}" "" _src "${_src}")
    string(FIND "${_src}" "${_zwrite_old}" _write_remaining)
    if(NOT _write_remaining EQUAL -1)
        message(FATAL_ERROR "actorlight33hf2: a legacy t_piza Z write remains after replacement")
    endif()

    file(WRITE "${_tpiza_c}" "${_src}")
    message(STATUS "actorlight33hf2: textured RGB888 Z tests patched without semicolon-count bug")
else()
    message(STATUS "actorlight33hf2: ZB/t_piza.c already patched")
endif()

# -------------------------------------------------------------------------
# ZB/tt24_piz.c: RGB888 flat/Gouraud solid triangles.
# -------------------------------------------------------------------------
file(READ "${_tt24_c}" _src)
set(_tt24_marker "ZbDepthTestWrite4DMM(z_ptr")
string(FIND "${_src}" "${_tt24_marker}" _already)
if(_already EQUAL -1)
    set(_cond_old "if (*z_ptr > BrFixedToInt(z_curr))")
    set(_cond_new "if (ZbDepthTestWrite4DMM(z_ptr, z_curr, BR_FALSE))")
    string(REGEX MATCHALL "if \\(\\*z_ptr > BrFixedToInt\\(z_curr\\)\\)" _matches "${_src}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL 4)
        message(FATAL_ERROR "actorlight32: expected 4 tt24 Z-test sites, found ${_count}")
    endif()
    string(REPLACE "${_cond_old}" "${_cond_new}" _src "${_src}")
    string(REPLACE "                    *z_ptr = BrFixedToInt(z_curr);\n" "" _src "${_src}")
    string(REPLACE "                br_uint_16 z_hi = z_curr >> 16;\n" "" _src "${_src}")
    file(WRITE "${_tt24_c}" "${_src}")
    message(STATUS "actorlight32: solid RGB888 Z tests use full 16.16 precision")
else()
    message(STATUS "actorlight32: ZB/tt24_piz.c already patched")
endif()


# -------------------------------------------------------------------------
# actorlight34: enormous/near-camera actors can produce scan spans outside the
# framebuffer even after the legacy clipper. Guard RGB888 writes rather than
# trusting pointer/index arithmetic inherited from the 1995 rasterizers.
# -------------------------------------------------------------------------
file(READ "${_tpiza_c}" _src)
set(_tpiza_guard_marker "ZbRasterPointersValid4DMM(ptr, zptr, bpp)")
string(FIND "${_src}" "${_tpiza_guard_marker}" _guarded)
if(_guarded EQUAL -1)
    set(_old "if (!ZbDepthTestWrite4DMM(zptr, (br_uint_32)zb.pz.currentpix, BR_TRUE))")
    set(_new "if (!ZbRasterPointersValid4DMM(ptr, zptr, bpp) || !ZbDepthTestWrite4DMM(zptr, (br_uint_32)zb.pz.currentpix, BR_TRUE))")
    string(REGEX MATCHALL "if \\(!ZbDepthTestWrite4DMM\\(zptr, \\(br_uint_32\\)zb\\.pz\\.currentpix, BR_TRUE\\)\\)" _matches "${_src}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL 2)
        message(FATAL_ERROR "actorlight34: expected 2 textured RGB888 depth sites for raster guard, found ${_count}")
    endif()
    string(REPLACE "${_old}" "${_new}" _src "${_src}")
    file(WRITE "${_tpiza_c}" "${_src}")
    message(STATUS "actorlight34: guarded textured RGB888 framebuffer/depth writes")
else()
    message(STATUS "actorlight34: textured RGB888 raster guard already applied")
endif()

file(READ "${_tt24_c}" _src)
set(_tt24_guard_marker "ZbRasterIndexValid4DMM(fb_curr)")
string(FIND "${_src}" "${_tt24_guard_marker}" _guarded)
if(_guarded EQUAL -1)
    set(_old "if (ZbDepthTestWrite4DMM(z_ptr, z_curr, BR_FALSE))")
    set(_new "if (ZbRasterIndexValid4DMM(fb_curr) && ZbDepthTestWrite4DMM(z_ptr, z_curr, BR_FALSE))")
    string(REGEX MATCHALL "if \\(ZbDepthTestWrite4DMM\\(z_ptr, z_curr, BR_FALSE\\)\\)" _matches "${_src}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL 4)
        message(FATAL_ERROR "actorlight34: expected 4 solid RGB888 depth sites for raster guard, found ${_count}")
    endif()
    string(REPLACE "${_old}" "${_new}" _src "${_src}")
    file(WRITE "${_tt24_c}" "${_src}")
    message(STATUS "actorlight34: guarded solid RGB888 framebuffer/depth writes")
else()
    message(STATUS "actorlight34: solid RGB888 raster guard already applied")
endif()
