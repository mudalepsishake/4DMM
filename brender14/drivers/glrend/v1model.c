/* BRenderModern:
 * Support routines for rendering models
 */
#include <string.h>
#include "drv.h"
#include "brassert.h"

void DeviceGLExtractPrimitiveState(const state_stack *state, br_primitive_state_info_gl *info, GLuint tex_white)
{
    const state_primitive *prim = &state->prim;

    // BRenderModern: clang-format off
    *info = (br_primitive_state_info_gl){
        .sampler = {
            .filter_min     = GL_NEAREST,
            .filter_mag     = GL_NEAREST,
            .wrap_s         = GL_REPEAT,
            .wrap_t         = GL_REPEAT,
        },
        .is_blended         = BR_FALSE,
        .is_filtered        = BR_FALSE,
        .is_indexed         = BR_FALSE,
        .disable_colour_key = BR_FALSE,
        .write_colour       = BR_TRUE,
        .write_depth        = BR_TRUE,
        .depth_func         = GL_LESS,
        .fog                = BR_FALSE,
        .fog_colour         = {{1.0f, 1.0f, 1.0f, 1.0f}},
        .fog_min            = 0,
        .fog_max            = 0,
        .fog_scale          = 1.0f,
        .shading_mode       = BRT_FLAT,
    };
    // BRenderModern: clang-format on

    if(!(state->valid & MASK_STATE_PRIMITIVE))
        return;

    info->is_blended         = (prim->flags & PRIMF_BLEND) != 0;
    info->disable_colour_key = (prim->flags & PRIMF_COLOUR_KEY) == 0;
    info->write_colour       = 1;
    info->write_depth        = (prim->flags & PRIMF_DEPTH_WRITE) != 0;
    info->is_indexed         = prim->colour_map ? (prim->colour_map->fmt->indexed != 0) : 0;
    info->fog                = prim->fog_type != BRT_NONE;
    info->shading_mode       = prim->shading_mode;

    if(prim->colour_map != NULL) {
        const br_buffer_stored *stored = prim->colour_map;

        info->is_indexed = stored->fmt->indexed;
        info->colour_map = BufferStoredGLGetTexture(stored);

        if(info->is_indexed) {
            info->colour_palette = BufferStoredGLGetCLUTTexture(stored, NULL, tex_white);
        } else {
            info->colour_palette = 0;
        }
    } else {
        info->is_indexed     = BR_FALSE;
        info->colour_map     = tex_white;
        info->colour_palette = 0;
    }

    /* BRenderModern: Don't depth write with transparent primitives. */
    info->write_depth = !info->is_blended && info->write_depth;

    if(prim->filter == BRT_LINEAR && prim->mip_filter == BRT_LINEAR) {
        info->sampler.filter_min = GL_LINEAR_MIPMAP_LINEAR;
        info->sampler.filter_mag = GL_LINEAR;

        info->is_filtered = 1;
    } else if(prim->filter == BRT_LINEAR && prim->mip_filter == BRT_NONE) {
        info->sampler.filter_min = GL_LINEAR;
        info->sampler.filter_mag = GL_LINEAR;

        info->is_filtered = 1;
    } else if(prim->filter == BRT_NONE && prim->mip_filter == BRT_LINEAR) {
        info->sampler.filter_min = GL_NEAREST_MIPMAP_NEAREST;
        info->sampler.filter_mag = GL_NEAREST;

        info->is_filtered = 0;
    } else if(prim->filter == BRT_NONE && prim->mip_filter == BRT_NONE) {
        info->sampler.filter_min = GL_NEAREST;
        info->sampler.filter_mag = GL_NEAREST;

        info->is_filtered = 0;
    } else {
        assert(0);
    }

    if(info->is_indexed) {
        info->sampler.filter_min = GL_NEAREST;
        info->sampler.filter_mag = GL_NEAREST;
    }

    /* BRenderModern:
     * Apply wrapping.
     */
    switch(prim->map_width_limit) {
        case BRT_WRAP:
        default:
            info->sampler.wrap_s = GL_REPEAT;
            break;

        case BRT_CLAMP:
            info->sampler.wrap_s = GL_CLAMP_TO_EDGE;
            break;

        case BRT_MIRROR:
            info->sampler.wrap_s = GL_MIRRORED_REPEAT;
            break;
    }

    switch(prim->map_height_limit) {
        case BRT_WRAP:
        default:
            info->sampler.wrap_t = GL_REPEAT;
            break;

        case BRT_CLAMP:
            info->sampler.wrap_t = GL_CLAMP_TO_EDGE;
            break;

        case BRT_MIRROR:
            info->sampler.wrap_t = GL_MIRRORED_REPEAT;
            break;
    }

    switch(prim->depth_test) {
        case BRT_LESS:
        default:
            info->depth_func = GL_LESS;
            break;

        case BRT_GREATER:
            info->depth_func = GL_GREATER;
            break;

        case BRT_LESS_OR_EQUAL:
            info->depth_func = GL_LEQUAL;
            break;

        case BRT_GREATER_OR_EQUAL:
            info->depth_func = GL_GEQUAL;
            break;

        case BRT_EQUAL:
            info->depth_func = GL_EQUAL;
            break;

        case BRT_NOT_EQUAL:
            info->depth_func = GL_NOTEQUAL;
            break;

        case BRT_NEVER:
            info->depth_func = GL_NEVER;
            break;

        case BRT_ALWAYS:
            info->depth_func = GL_ALWAYS;
            break;
    }

    if(prim->fog_type != BRT_NONE) {
        info->fog_colour.v[0] = BR_RED(prim->fog_colour) / 255.0f;
        info->fog_colour.v[1] = BR_GRN(prim->fog_colour) / 255.0f;
        info->fog_colour.v[2] = BR_BLU(prim->fog_colour) / 255.0f;
        info->fog_colour.v[3] = 1.0f;

        info->fog_min   = BrGLScalarToFloat(prim->fog_min);
        info->fog_max   = BrGLScalarToFloat(prim->fog_max);
        info->fog_scale = (float)prim->fog_scale / 255.0f;
    }
}

static void apply_blend_mode(state_stack *self, const GladGLContext *gl)
{
    /* BRenderModern: C_result = (C_source * F_Source) + (C_dest * F_dest) */

    /* BRenderModern: NB: srcAlpha and dstAlpha are all GL_ONE and GL_ZERO respectively. */
    switch(self->prim.blend_mode) {
        default:
            /* BRenderModern: fallthrough */
        case BRT_BLEND_STANDARD:
            /* BRenderModern: fallthrough */
        case BRT_BLEND_DIMMED:
            /* BRenderModern:
             * 3dfx blending mode = 1
             * Colour = (alpha * src) + ((1 - alpha) * dest)
             * Alpha  = (1     * src) + (0           * dest)
             */
            gl->BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
            break;

        case BRT_BLEND_SUMMED:
            /* BRenderModern:
             * 3fdx blending mode = 4
             * Colour = (alpha * src) + (1 * dest)
             * Alpha  = (1     * src) + (0 * dest)
             */
            gl->BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
            break;

        case BRT_BLEND_PREMULTIPLIED:
            /* BRenderModern:
             * 3dfx qblending mode = 2
             * Colour = (1 * src) + ((1 - alpha) * dest)
             * Alpha  = (1 * src) + (0           * dest)
             */
            gl->BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
            break;
    }
}

static void apply_cull_property(const GladGLContext *gl, const state_stack *state)
{
    /* BRenderModern:
     * Apply culling states. These are a bit confusing:
     * BRT_ONE_SIDED - Simple, cull back faces. From BRT_ONE_SIDED.
     *
     * BRT_TWO_SIDED - This means the face is two-sided, not to cull
     *                 both sides. From BR_MATF_TWO_SIDED. In the .3ds file
     *                 format, the "two sided" flag means the material is
     *                 visible from the back, or "not culled". fmt/load3ds.c
     *                 sets BR_MATF_TWO_SIDED if this is set, so assume this is
     *                 the correct behaviour.
     *
     * BRT_NONE      - Confusing, this is set if the material has
     *                 BR_MATF_ALWAYS_VISIBLE, but is overridden if
     *                 BR_MATF_TWO_SIDED is set. Assume it means the same
     *                 as BR_MATF_TWO_SIDED.
     */
    switch(state->cull.type) {
        case BRT_ONE_SIDED:
        default: /* BRenderModern: Default BRender policy, so default. */
            gl->Enable(GL_CULL_FACE);
            gl->CullFace(GL_BACK);
            break;

        case BRT_TWO_SIDED:
        case BRT_NONE:
            gl->Disable(GL_CULL_FACE);
            break;
    }
}

static void apply_stored_properties(const GladGLContext *gl, br_renderer *renderer, state_stack *state, uint32_t states,
                                    br_gl_main_data_model *model, GLuint tex_default)
{
    br_boolean depth_test = BR_FALSE;

    /* BRenderModern: Only use the states we want (if valid). */
    states = state->valid & states;

    if(states & MASK_STATE_CULL)
        apply_cull_property(gl, state);

    model->lighting    = BR_FALSE;
    model->prelighting = BR_FALSE;

    if(states & MASK_STATE_SURFACE) {
        if(state->surface.colour_source == BRT_SURFACE) {
            br_uint_32 colour = state->surface.colour;
            float      r      = BR_RED(colour) / 255.0f;
            float      g      = BR_GRN(colour) / 255.0f;
            float      b      = BR_BLU(colour) / 255.0f;
            BrGLVector4FSet(&model->surface_colour, r, g, b, BrGLScalarToFloat(state->surface.opacity));

            model->colour_source = 1;
        } else {
            ASSERT(state->surface.colour_source == BRT_GEOMETRY);
            BrGLVector4FSet(&model->surface_colour, 1.0f, 1.0f, 1.0f, BrGLScalarToFloat(state->surface.opacity));
            model->colour_source = 0;
        }

        model->ka    = BrGLScalarToFloat(state->surface.ka);
        model->ks    = BrGLScalarToFloat(state->surface.ks);
        model->kd    = BrGLScalarToFloat(state->surface.kd);
        model->power = BrGLScalarToFloat(state->surface.power);

        switch(state->surface.mapping_source) {
            case BRT_GEOMETRY_MAP:
            default:
                model->uv_source = 0;
                break;

            case BRT_ENVIRONMENT_LOCAL:
                model->uv_source = 1;
                break;

            case BRT_ENVIRONMENT_INFINITE:
                model->uv_source = 2;
                break;

            case BRT_GEOMETRY_X:
                model->uv_source = 3;
                break;

            case BRT_GEOMETRY_Y:
                model->uv_source = 4;
                break;

            case BRT_GEOMETRY_Z:
                model->uv_source = 5;
                break;
        }

        BrGLMatrix23ToMatrix4Float(&model->map_transform, &state->surface.map_transform);

        depth_test = !state->surface.force_front && !state->surface.force_back;

        model->lighting    = state->surface.lighting;
        model->prelighting = state->surface.prelighting;
    }

    {
        br_primitive_state_info_gl info;
        DeviceGLExtractPrimitiveState(state, &info, tex_default);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
        /*
         * 4DMM/3DMM authored materials are predominantly flat/Gouraud.  The
         * modern GL driver already has a complete Phong path which interpolates
         * position/normal and calls accumulateLights() in the fragment shader.
         * Promote the legacy material request here so dynamic lighting is
         * evaluated per pixel without changing the stored movie/material data.
         */
        model->shading_mode = 2;
#else
        switch(info.shading_mode) {
            case BRT_FLAT:
            default:
                model->shading_mode = 1; // BRenderModern: FIXME: 0 once I figure out flat
                break;

            case BRT_GOURAUD:
                model->shading_mode = 1;
                break;

            case BRT_PHONG:
                model->shading_mode = 2;
                break;
        }
#endif

        model->disable_colour_key = info.disable_colour_key;

        if(info.write_colour) {
            gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        } else {
            gl->ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        }

        gl->BindSampler(0, RendererGLGetSampler(renderer, &info.sampler));

        if(info.is_indexed) {
            model->texture_mode = info.is_filtered ? 2 : 1;

            gl->ActiveTexture(GL_TEXTURE1);
            gl->BindTexture(GL_TEXTURE_2D, info.colour_map);

            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, info.colour_palette);
        } else {
            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, info.colour_map);

            model->texture_mode = 0;
        }

        if(info.is_blended) {
            gl->Enable(GL_BLEND);
            apply_blend_mode(state, gl);
        } else {
            gl->Disable(GL_BLEND);
        }

        gl->DepthFunc(info.depth_func);
        gl->DepthMask(info.write_depth ? GL_TRUE : GL_FALSE);

        model->enable_fog = info.fog;
        model->fog_colour      = info.fog_colour;
        model->fog_scale       = info.fog_scale;
        model->fog_range.v[0]  = info.fog_min;
        model->fog_range.v[1]  = info.fog_max;
    }

    if(states & MASK_STATE_OUTPUT) {
        if(state->output.depth == NULL)
            depth_test = BR_FALSE;
    }

    if(depth_test == BR_TRUE)
        gl->Enable(GL_DEPTH_TEST);
    else
        gl->Disable(GL_DEPTH_TEST);
}

static br_boolean apply_state(br_renderer *renderer, const GladGLContext *gl)
{
    const br_gl_context_state *ctx   = GLContextState(gl);
    state_cache               *cache = &renderer->state.cache;
    br_boolean                 unlit;
    br_gl_main_data_model      model = {0};

    /* BRenderModern: Update the per-model cache (matrices and lights) */
    StateGLUpdateModel(cache, &renderer->state.current->matrix);

    BrGLMatrix4ToFloat(&model.projection, &cache->model.p);
    BrGLMatrix4ToFloat(&model.model_view, &cache->model.mv);
    BrGLMatrix4ToFloat(&model.mvp, &cache->model.mvp);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
    if(renderer->state.current->output.shadow_model_to_light_valid) {
        br_matrix4 model_to_light4;
        BrMatrix4Copy34(&model_to_light4, &renderer->state.current->output.shadow_model_to_light);
        BrGLMatrix4ToFloat(&model.shadow_model_to_light, &model_to_light4);
    } else if(cache->shadow_view_to_light_valid) {
        /* v218 fallback for non-3DMM callers that do not provide a direct actor transform. */
        br_matrix34 model_to_light;
        br_matrix4 model_to_light4;
        BrMatrix34Mul(&model_to_light, &renderer->state.current->matrix.model_to_view,
                      &cache->shadow_view_to_light);
        BrMatrix4Copy34(&model_to_light4, &model_to_light);
        BrGLMatrix4ToFloat(&model.shadow_model_to_light, &model_to_light4);
    }
#endif
    BrGLMatrix4ToFloat(&model.normal_matrix, &cache->model.normal);
    BrGLMatrix4ToFloat(&model.environment_matrix, &cache->model.environment);
    BrGLVector4ToFloat(&model.eye_m, &cache->model.eye_m);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    /* v232 packs receiver policy above the 8-bit owner slot in the legacy
     * SHADOW_OWNER_U32 state, then splits it back into the per-model UBO. */
    model.shadow_owner_info.v[0] = (br_int_32)(renderer->state.current->output.shadow_owner & 0xFFu);
    model.shadow_owner_info.v[1] = (br_int_32)((renderer->state.current->output.shadow_owner >> 8) & 0xFFu);
    model.shadow_owner_info.v[2] = 0;
    model.shadow_owner_info.v[3] = 0;

    if(renderer->state.current->output.shadow_pass) {
        if(!RendererGLSelectShadowPass(renderer, BR_TRUE))
            return BR_FALSE;

        /*
         * v270: the shadow pass used to force GL_CULL_FACE off globally and
         * return before the material's ordinary BRT_CULL state was applied.
         * That made back faces/interior-facing geometry cast shadows even when
         * the same material is one-sided in the colour pass. It is especially
         * visible on imported architectural meshes as window-shaped "light"
         * holes in a false self-shadow cast by the building's inward faces.
         *
         * Mirror the normal model path here: preserve reflected-transform
         * winding and then honor the material cull state. Legitimate two-sided
         * materials remain two-sided; one-sided materials no longer acquire a
         * secret second shadow-casting side.
         */
        if(BrScalarToFloat(cache->model.mv_det3) < 1e-6f)
            gl->FrontFace(GL_CW);
        else
            gl->FrontFace(GL_CCW);
        apply_cull_property(gl, renderer->state.current);

        return BufferRingGLPush(&renderer->model_ring, &model, sizeof(model));
    }
    (void)RendererGLSelectShadowPass(renderer, BR_FALSE);
#endif

    /* BRenderModern:
     * If we've got a negative determinant for the upper 3x3 MV matrix,
     * we're reflecting, i.e. changing handedness.
     * To keep the same behaviour as the software renderer, flip the winding order.
     */
    if(BrScalarToFloat(cache->model.mv_det3) < 1e-6f)
        gl->FrontFace(GL_CW);
    else
        gl->FrontFace(GL_CCW);

    /* BRenderModern: NB: Flag is never set */
    // BRenderModern: int model_lit = self->model->flags & V11MODF_LIT;

    apply_stored_properties(gl, renderer, renderer->state.current, MASK_STATE_STORED | MASK_STATE_OUTPUT, &model, ctx->tex_white);

    switch(renderer->state.current->render_type) {
        case BRT_POINT:
            gl->PolygonMode(GL_FRONT_AND_BACK, GL_POINT);
            break;

        case BRT_TRIANGLE:
        default:
            gl->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            break;
    }

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    {
        static br_uint_32 diag_state_count;
        if(diag_state_count < 128) {
            BrWarning("3DMM glrend apply-state n=%u opacity_raw=0x%08X opacity=%g m2v_t=(%g,%g,%g) mvp_row0=(%g,%g,%g,%g) mvp_row3=(%g,%g,%g,%g)",
                      (unsigned)diag_state_count, (unsigned)(br_uint_32)renderer->state.current->surface.opacity,
                      (double)BrScalarToFloat(renderer->state.current->surface.opacity),
                      (double)BrScalarToFloat(renderer->state.current->matrix.model_to_view.m[3][0]),
                      (double)BrScalarToFloat(renderer->state.current->matrix.model_to_view.m[3][1]),
                      (double)BrScalarToFloat(renderer->state.current->matrix.model_to_view.m[3][2]),
                      (double)model.mvp.m[0][0], (double)model.mvp.m[0][1], (double)model.mvp.m[0][2], (double)model.mvp.m[0][3],
                      (double)model.mvp.m[3][0], (double)model.mvp.m[3][1], (double)model.mvp.m[3][2], (double)model.mvp.m[3][3]);
            ++diag_state_count;
        }
    }
#endif

    return BufferRingGLPush(&renderer->model_ring, &model, sizeof(model));
}

#if BRENDER_LEGACY_3DMM_MODEL_ABI
static void RendererGL3DMMDiagDraw(const GladGLContext *gl, const char *kind, br_uint_32 primitives, br_uint_32 vertices)
{
    static br_uint_32 diag_draw_count;
    if(diag_draw_count >= 128)
        return;

    GLint program = 0, fbo = 0, depth_func = 0, front_face = 0, cull_mode = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLboolean depth_mask = GL_FALSE;
    GLboolean colour_mask[4] = {GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE};
    gl->GetIntegerv(GL_CURRENT_PROGRAM, &program);
    gl->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    gl->GetIntegerv(GL_VIEWPORT, viewport);
    gl->GetIntegerv(GL_DEPTH_FUNC, &depth_func);
    gl->GetIntegerv(GL_FRONT_FACE, &front_face);
    gl->GetIntegerv(GL_CULL_FACE_MODE, &cull_mode);
    gl->GetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    gl->GetBooleanv(GL_COLOR_WRITEMASK, colour_mask);
    GLenum error = gl->GetError();

    BrWarning("3DMM glrend draw n=%u kind=%s primitives=%u vertices=%u program=%d fbo=%d viewport=%d,%d,%d,%d depth=%d func=0x%X write=%d cull=%d mode=0x%X front=0x%X blend=%d colourmask=%d%d%d%d glerr=0x%X",
              (unsigned)diag_draw_count, kind, (unsigned)primitives, (unsigned)vertices, (int)program, (int)fbo,
              (int)viewport[0], (int)viewport[1], (int)viewport[2], (int)viewport[3],
              (int)gl->IsEnabled(GL_DEPTH_TEST), (unsigned)depth_func, (int)depth_mask,
              (int)gl->IsEnabled(GL_CULL_FACE), (unsigned)cull_mode, (unsigned)front_face,
              (int)gl->IsEnabled(GL_BLEND), (int)colour_mask[0], (int)colour_mask[1],
              (int)colour_mask[2], (int)colour_mask[3], (unsigned)error);
    ++diag_draw_count;
}
#endif

void RendererGLRenderGroup(br_renderer *self, br_geometry_stored *stored, const gl_groupinfo *groupinfo)
{
    const GladGLContext *gl = self->gl;

    if(!apply_state(self, gl)) {
        BrLogWarn("GLREND", "Out of model space.");
        return;
    }

    gl->BindVertexArray(stored->gl_vao);
    const br_boolean shadow_pass = self->shadow_pass_active;

    switch(self->state.current->render_type) {
        case BRT_POINT:
            gl->DrawArrays(GL_POINTS, (GLint)(groupinfo->vertex_offset / sizeof(gl_vertex_f)), groupinfo->group->nvertices);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
            if(!shadow_pass)
                RendererGL3DMMDiagDraw(gl, "point-group", groupinfo->group->nvertices, groupinfo->group->nvertices);
#endif
            if(!shadow_pass)
                self->stats.vertices_rendered_count += groupinfo->group->nvertices;
            break;

        case BRT_LINE:
            if(groupinfo->line_count > 0)
                gl->DrawElements(GL_LINES, groupinfo->line_count, GL_UNSIGNED_SHORT, groupinfo->line_offset);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
            if(!shadow_pass)
                RendererGL3DMMDiagDraw(gl, "line-group", (br_uint_32)groupinfo->line_count / 2, (br_uint_32)groupinfo->line_count);
#endif
            if(!shadow_pass)
                self->stats.vertices_rendered_count += groupinfo->line_count;
            break;

        case BRT_TRIANGLE:
        default:
            gl->DrawElements(GL_TRIANGLES, groupinfo->count, GL_UNSIGNED_SHORT, groupinfo->offset);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
            if(!shadow_pass)
                RendererGL3DMMDiagDraw(gl, "triangle-group", groupinfo->group->nfaces, groupinfo->group->nvertices);
#endif
            if(!shadow_pass) {
                self->stats.triangles_rendered_count += groupinfo->group->nfaces;
                self->stats.triangles_drawn_count += groupinfo->group->nfaces;
                self->stats.vertices_rendered_count += groupinfo->group->nfaces * 3;
            }
            break;
    }

    if(!shadow_pass) {
        self->stats.face_group_count++;
        self->stats.opaque_draw_count += 1;
    }
}

void RendererGLRenderTri(br_renderer *self, br_uintptr_t offset, const gl_groupinfo *groupinfo)
{
    const GladGLContext *gl = self->gl;

    if(!apply_state(self, gl)) {
        BrLogWarn("GLREND", "Out of model space.");
        return;
    }

    gl->BindVertexArray(self->trans.vao);
    gl->BindBuffer(GL_ARRAY_BUFFER, self->trans.vbo);
    gl->DrawArrays(GL_TRIANGLES, (GLint)offset, 3);

    if(!self->shadow_pass_active) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        RendererGL3DMMDiagDraw(gl, "triangle", 1, 3);
#endif
        self->stats.face_group_count++;
        self->stats.triangles_rendered_count += 1;
        self->stats.triangles_drawn_count += 1;
        self->stats.vertices_rendered_count += 3;
        self->stats.transparent_draw_count += 1;
    }
}
