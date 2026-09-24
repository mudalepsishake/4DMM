cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED BRENDER_SOURCE_DIR)
    message(FATAL_ERROR "actorlight5: BRENDER_SOURCE_DIR was not provided")
endif()

set(_file "${BRENDER_SOURCE_DIR}/ZB/t_piza.c")
if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "actorlight5: cannot find ${_file}")
endif()

file(READ "${_file}" _src)

set(_marker "actorlight7: RGB888 textured lighting")
string(FIND "${_src}" "${_marker}" _already)

set(_old_forward [[                    for (int i = 0; i < bpp; i++)
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }]])

set(_old_backward [[                if (use_light)
                {
                    *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                }]])

set(_actorlight2_forward [[                    // actorlight2: RGB888 textured lighting.  The source BRender
                    // implementation still treated a 24-bit texel as one INDEX_8
                    // palette index here, which dereferenced index_shade and crashed.
                    // For RGB888, modulate each B,G,R texture channel directly by
                    // the interpolated scalar light intensity.  Preserve the old
                    // shade-table path exactly for indexed rendering.
                    if (bpp == 3)
                    {
                        const br_uint_32 intensity = BrFixedToInt(zb.pi.currentpix) & 0xFF;
                        for (int i = 0; i < 3; i++)
                            ptr[i] = (br_uint_8)(((br_uint_32)src[2 - i] * intensity + 127) / 255);
                    }
                    else
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }]])

set(_actorlight2_backward [[                if (use_light)
                {
                    // actorlight2: mirror the forward RGB888 lighting path when
                    // rasterising right-to-left.
                    if (bpp == 3)
                    {
                        const br_uint_32 intensity = BrFixedToInt(zb.pi.currentpix) & 0xFF;
                        for (int i = 0; i < 3; i++)
                            ptr[i] = (br_uint_8)(((br_uint_32)src[2 - i] * intensity + 127) / 255);
                    }
                    else
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }
                }]])

set(_actorlight3_forward [[                    // actorlight3: RGB888 textured lighting.  4DMM expands
                    // texture maps into B,G,R bytes, the same order used by the
                    // Windows 24-bit DIB target.  Modulate those bytes in place;
                    // do not reverse R/B a second time.  Preserve the original
                    // INDEX_8 shade-table path exactly.
                    if (bpp == 3)
                    {
                        const br_uint_32 intensity = BrFixedToInt(zb.pi.currentpix) & 0xFF;
                        for (int i = 0; i < 3; i++)
                            ptr[i] = (br_uint_8)(((br_uint_32)src[i] * intensity + 127) / 255);
                    }
                    else
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }]])

set(_actorlight3_backward [[                if (use_light)
                {
                    // actorlight3: mirror the forward RGB888 lighting path when
                    // rasterising right-to-left.
                    if (bpp == 3)
                    {
                        const br_uint_32 intensity = BrFixedToInt(zb.pi.currentpix) & 0xFF;
                        for (int i = 0; i < 3; i++)
                            ptr[i] = (br_uint_8)(((br_uint_32)src[i] * intensity + 127) / 255);
                    }
                    else
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }
                }]])

set(_actorlight4_forward [[                    // actorlight4: RGB888 textured lighting with a soft
                    // response. Preserve at least ~55% of the source texture
                    // even when BRender's interpolated light reaches zero, and
                    // use the remaining range for scene lighting. This keeps
                    // low-poly actor faces dimensional without the harsh black
                    // polygon patches from the first experiment.
                    if (bpp == 3)
                    {
                        const br_uint_32 raw = BrFixedToInt(zb.pi.currentpix) & 0xFF;
                        const br_uint_32 intensity = 140 + ((raw * 115 + 127) / 255);
                        for (int i = 0; i < 3; i++)
                            ptr[i] = (br_uint_8)(((br_uint_32)src[i] * intensity + 127) / 255);
                    }
                    else
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }]])

set(_actorlight4_backward [[                if (use_light)
                {
                    // actorlight4: mirror the softened forward RGB888 lighting
                    // response when rasterising right-to-left.
                    if (bpp == 3)
                    {
                        const br_uint_32 raw = BrFixedToInt(zb.pi.currentpix) & 0xFF;
                        const br_uint_32 intensity = 140 + ((raw * 115 + 127) / 255);
                        for (int i = 0; i < 3; i++)
                            ptr[i] = (br_uint_8)(((br_uint_32)src[i] * intensity + 127) / 255);
                    }
                    else
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }
                }]])

set(_actorlight5_forward [[                    // actorlight5: RGB888 textured lighting with a medium
                    // response. Preserve 30% of the source texture at minimum
                    // and leave 70% of the range available to scene lighting.
                    if (bpp == 3)
                    {
                        const br_uint_32 raw = BrFixedToInt(zb.pi.currentpix) & 0xFF;
                        const br_uint_32 intensity = 77 + ((raw * 178 + 127) / 255);
                        for (int i = 0; i < 3; i++)
                            ptr[i] = (br_uint_8)(((br_uint_32)src[i] * intensity + 127) / 255);
                    }
                    else
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }]])

set(_actorlight5_backward [[                if (use_light)
                {
                    // actorlight5: mirror the medium-strength forward RGB888
                    // lighting response when rasterising right-to-left.
                    if (bpp == 3)
                    {
                        const br_uint_32 raw = BrFixedToInt(zb.pi.currentpix) & 0xFF;
                        const br_uint_32 intensity = 77 + ((raw * 178 + 127) / 255);
                        for (int i = 0; i < 3; i++)
                            ptr[i] = (br_uint_8)(((br_uint_32)src[i] * intensity + 127) / 255);
                    }
                    else
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }
                }]])

set(_new_forward [[                    // actorlight7: RGB888 textured lighting keeps actorlight6's smooth
                    // normals and 30% true shadow floor, but remaps BRender's raw light with
                    // a gentle ease-out curve.  Mid-lit faces become much more
                    // visible while genuinely unlit faces can remain dark.
                    if (bpp == 3)
                    {
                        const br_uint_32 raw = BrFixedToInt(zb.pi.currentpix) & 0xFF;
                        const br_uint_32 inv = 255 - raw;
                        const br_uint_32 lifted = 255 - ((inv * inv + 127) / 255);
                        const br_uint_32 intensity = 77 + ((lifted * 178 + 127) / 255);
                        for (int i = 0; i < 3; i++)
                            ptr[i] = (br_uint_8)(((br_uint_32)src[i] * intensity + 127) / 255);
                    }
                    else
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }]])

set(_new_backward [[                if (use_light)
                {
                    // actorlight7: mirror the forward ease-out RGB888 lighting
                    // response when rasterising right-to-left.
                    if (bpp == 3)
                    {
                        const br_uint_32 raw = BrFixedToInt(zb.pi.currentpix) & 0xFF;
                        const br_uint_32 inv = 255 - raw;
                        const br_uint_32 lifted = 255 - ((inv * inv + 127) / 255);
                        const br_uint_32 intensity = 77 + ((lifted * 178 + 127) / 255);
                        for (int i = 0; i < 3; i++)
                            ptr[i] = (br_uint_8)(((br_uint_32)src[i] * intensity + 127) / 255);
                    }
                    else
                    {
                        *ptr = zb.shade_table[*src | ((BrFixedToInt(zb.pi.currentpix) & 0xFF) << 8)];
                    }
                }]])

if(_already EQUAL -1)
    string(FIND "${_src}" "actorlight5: RGB888 textured lighting" _has_actorlight5)
    string(FIND "${_src}" "actorlight4: RGB888 textured lighting" _has_actorlight4)
    string(FIND "${_src}" "actorlight3: RGB888 textured lighting" _has_actorlight3)
    string(FIND "${_src}" "actorlight2: RGB888 textured lighting" _has_actorlight2)

    if(NOT _has_actorlight5 EQUAL -1)
        string(FIND "${_src}" "${_actorlight5_forward}" _found_forward)
        string(FIND "${_src}" "${_actorlight5_backward}" _found_backward)
        if(_found_forward EQUAL -1 OR _found_backward EQUAL -1)
            message(FATAL_ERROR "actorlight7: found actorlight5 marker but not its expected RGB888 blocks")
        endif()
        string(REPLACE "${_actorlight5_forward}" "${_new_forward}" _src "${_src}")
        string(REPLACE "${_actorlight5_backward}" "${_new_backward}" _src "${_src}")
    elseif(NOT _has_actorlight4 EQUAL -1)
        string(FIND "${_src}" "${_actorlight4_forward}" _found_forward)
        string(FIND "${_src}" "${_actorlight4_backward}" _found_backward)
        if(_found_forward EQUAL -1 OR _found_backward EQUAL -1)
            message(FATAL_ERROR "actorlight7: found actorlight4 marker but not its expected RGB888 blocks")
        endif()
        string(REPLACE "${_actorlight4_forward}" "${_new_forward}" _src "${_src}")
        string(REPLACE "${_actorlight4_backward}" "${_new_backward}" _src "${_src}")
    elseif(NOT _has_actorlight3 EQUAL -1)
        string(FIND "${_src}" "${_actorlight3_forward}" _found_forward)
        string(FIND "${_src}" "${_actorlight3_backward}" _found_backward)
        if(_found_forward EQUAL -1 OR _found_backward EQUAL -1)
            message(FATAL_ERROR "actorlight7: found actorlight3 marker but not its expected RGB888 blocks")
        endif()
        string(REPLACE "${_actorlight3_forward}" "${_new_forward}" _src "${_src}")
        string(REPLACE "${_actorlight3_backward}" "${_new_backward}" _src "${_src}")
    elseif(NOT _has_actorlight2 EQUAL -1)
        string(FIND "${_src}" "${_actorlight2_forward}" _found_forward)
        string(FIND "${_src}" "${_actorlight2_backward}" _found_backward)
        if(_found_forward EQUAL -1 OR _found_backward EQUAL -1)
            message(FATAL_ERROR "actorlight7: found actorlight2 marker but not its expected RGB888 blocks")
        endif()
        string(REPLACE "${_actorlight2_forward}" "${_new_forward}" _src "${_src}")
        string(REPLACE "${_actorlight2_backward}" "${_new_backward}" _src "${_src}")
    else()
        string(FIND "${_src}" "${_old_forward}" _found_forward)
        string(FIND "${_src}" "${_old_backward}" _found_backward)
        if(_found_forward EQUAL -1 OR _found_backward EQUAL -1)
            message(FATAL_ERROR "actorlight7: expected clean/actorlight2/3/4/5 blazin/ZB/t_piza.c; refusing unknown BRender revision")
        endif()
        string(REPLACE "${_old_forward}" "${_new_forward}" _src "${_src}")
        string(REPLACE "${_old_backward}" "${_new_backward}" _src "${_src}")
    endif()
else()
    message(STATUS "actorlight7: RGB888 textured lighting response already applied")
endif()

set(_old_transparency "if (use_transparency && *src == 0)")
set(_new_transparency "if (use_transparency && ((bpp == 3) ? ((src[0] | src[1] | src[2]) == 0) : (*src == 0)))")
string(REGEX MATCHALL "if \\(use_transparency && \\*src == 0\\)" _legacy_transparency_matches "${_src}")
list(LENGTH _legacy_transparency_matches _legacy_transparency_count)
string(REGEX MATCHALL "if \\(use_transparency && \\(\\(bpp == 3\\) \\? \\(\\(src\\[0\\] \\| src\\[1\\] \\| src\\[2\\]\\) == 0\\) : \\(\\*src == 0\\)\\)\\)" _rgb_transparency_matches "${_src}")
list(LENGTH _rgb_transparency_matches _rgb_transparency_count)

if(_rgb_transparency_count EQUAL 2)
    # actorlight3+ already fixed RGB888 transparency.
elseif(_legacy_transparency_count EQUAL 2)
    string(REPLACE "${_old_transparency}" "${_new_transparency}" _src "${_src}")
else()
    message(FATAL_ERROR "actorlight5: expected either two legacy or two RGB888 transparency tests")
endif()

file(WRITE "${_file}" "${_src}")

# Source BRender's solid RGB888 rasterizer writes R,G,B at offsets 0,1,2,
# while 4DMM's Windows 24-bit target is B,G,R. Fix both flat and Gouraud
# solid writes. This is separate from textured actor lighting.
set(_solid_file "${BRENDER_SOURCE_DIR}/ZB/tt24_piz.c")
if(NOT EXISTS "${_solid_file}")
    message(FATAL_ERROR "actorlight5: cannot find ${_solid_file}")
endif()
file(READ "${_solid_file}" _solid_src)
set(_solid_marker "actorlight5: Windows BGR RGB888 solids")
string(FIND "${_solid_src}" "${_solid_marker}" _solid_already)
if(_solid_already EQUAL -1)
    set(_solid_old [[                    zb.colour_buffer[fb_curr * 3 + 0] = BrFixedToInt(temp_r);
                    zb.colour_buffer[fb_curr * 3 + 1] = BrFixedToInt(temp_g);
                    zb.colour_buffer[fb_curr * 3 + 2] = BrFixedToInt(temp_b);]])
    set(_solid_new [[                    // actorlight5: Windows BGR RGB888 solids
                    zb.colour_buffer[fb_curr * 3 + 0] = BrFixedToInt(temp_b);
                    zb.colour_buffer[fb_curr * 3 + 1] = BrFixedToInt(temp_g);
                    zb.colour_buffer[fb_curr * 3 + 2] = BrFixedToInt(temp_r);]])
    # Apply the BGR conversion by matching the complete three-line RGB888
    # write block. The previous actorlight5 safety check counted individual
    # lines with CMake regex and could report twice the real number of blocks.
    # All we actually require is at least one complete block and zero complete
    # R,G,B blocks remaining after replacement.
    string(FIND "${_solid_src}" "${_solid_old}" _solid_found)
    if(_solid_found EQUAL -1)
        message(FATAL_ERROR "actorlight5b: no complete RGB888 solid R,G,B write block found")
    endif()

    string(REPLACE "${_solid_old}" "${_solid_new}" _solid_src "${_solid_src}")

    string(FIND "${_solid_src}" "${_solid_old}" _solid_remaining)
    if(NOT _solid_remaining EQUAL -1)
        message(FATAL_ERROR "actorlight5b: an RGB888 solid R,G,B write block remains after patch")
    endif()

    message(STATUS "actorlight5b: RGB888 solid writes patched to Windows BGR order")
    file(WRITE "${_solid_file}" "${_solid_src}")
else()
    message(STATUS "actorlight5: solid RGB888 BGR patch already applied")
endif()

message(STATUS "actorlight7: smooth actor lighting + lifted midtones + RGB888 transparency + solid BGR ready")


# actorlight9: the RGB888 lit-texture material table requests/interpolates C_I,
# but true-colour LightingColour() in Source BRender only populates C_R/C_G/C_B.
# That left C_I stale/uninitialised, which made textured shading depend on
# unrelated render state/object order and prevented local point lights from
# producing trustworthy distance-based illumination.  Populate C_I from the
# final clamped RGB lighting result.  4DMM mapped materials use a white base
# colour and white scene lights, so the three channels are normally equal;
# averaging also behaves sensibly if coloured lights are added later.
set(_light24_file "${BRENDER_SOURCE_DIR}/FW/light24.c")
if(NOT EXISTS "${_light24_file}")
    message(FATAL_ERROR "actorlight9: cannot find ${_light24_file}")
endif()
file(READ "${_light24_file}" _light24_src)
set(_light24_marker "actorlight9: feed RGB888 lit textures a real scalar C_I")
string(FIND "${_light24_src}" "${_light24_marker}" _light24_already)
if(_light24_already EQUAL -1)
    set(_light24_old [[    if (comp[C_B] >= RGB_MAX)
        comp[C_B] = RGB_MAX;
    else if (comp[C_B] < RGB_MIN)
        comp[C_B] = RGB_MIN;
}]])
    set(_light24_new [[    if (comp[C_B] >= RGB_MAX)
        comp[C_B] = RGB_MAX;
    else if (comp[C_B] < RGB_MIN)
        comp[C_B] = RGB_MIN;

    // actorlight9: feed RGB888 lit textures a real scalar C_I.
    // ZB/zbsetup.c requests CM_I for the RGB888 TIA texture primitive and
    // ZB/awtmz.c interpolates comp[C_I], but LightingColour historically
    // only wrote C_R/C_G/C_B.  Leaving C_I untouched made texture lighting
    // read stale vertex-component data.  Average the final clamped lighting
    // channels into the 0..254 scalar intensity expected by the mapper.
    comp[C_I] = BR_CONST_DIV(comp[C_R] + comp[C_G] + comp[C_B], 3);
}]])
    string(FIND "${_light24_src}" "${_light24_old}" _light24_found)
    if(_light24_found EQUAL -1)
        message(FATAL_ERROR "actorlight9: expected LightingColour RGB clamp tail not found")
    endif()
    string(REPLACE "${_light24_old}" "${_light24_new}" _light24_src "${_light24_src}")
    file(WRITE "${_light24_file}" "${_light24_src}")
    message(STATUS "actorlight9: RGB888 lit-texture C_I feed patched into FW/light24.c")
else()
    message(STATUS "actorlight9: RGB888 lit-texture C_I feed already applied")
endif()
