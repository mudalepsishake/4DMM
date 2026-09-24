/* BRenderModern:
 * Renderer methods
 */
#include "drv.h"
#include "brassert.h"

/* BRenderModern:
 * Default dispatch table for renderer (defined at end of file)
 */
static const struct br_renderer_dispatch rendererDispatch;

#define F(f) offsetof(struct br_renderer, f)

static br_tv_template_entry rendererTemplateEntries[] = {
    {BRT(IDENTIFIER_CSTR),              F(identifier),                     BRTV_QUERY | BRTV_ALL, BRTV_CONV_COPY},
    {BRT(FACE_GROUP_COUNT_U32),         F(stats.face_group_count),         BRTV_QUERY | BRTV_ALL, BRTV_CONV_COPY},
    {BRT(TRIANGLES_DRAWN_COUNT_U32),    F(stats.triangles_drawn_count),    BRTV_QUERY | BRTV_ALL, BRTV_CONV_COPY},
    {BRT(TRIANGLES_RENDERED_COUNT_U32), F(stats.triangles_rendered_count), BRTV_QUERY | BRTV_ALL, BRTV_CONV_COPY},
    {BRT(VERTICES_RENDERED_COUNT_U32),  F(stats.vertices_rendered_count),  BRTV_QUERY | BRTV_ALL, BRTV_CONV_COPY},
    {BRT(OPAQUE_DRAW_COUNT_U32),        F(stats.opaque_draw_count),        BRTV_QUERY | BRTV_ALL, BRTV_CONV_COPY},
    {BRT(TRANSPARENT_DRAW_COUNT_U32),   F(stats.transparent_draw_count),   BRTV_QUERY | BRTV_ALL, BRTV_CONV_COPY},
};
#undef F

static void RendererGLInitImm(br_renderer *self, br_gl_main_shader *shader)
{
    const GladGLContext *gl = self->gl;

    gl->GenVertexArrays(1, &self->trans.vao);
    gl->BindVertexArray(self->trans.vao);

    gl->GenBuffers(1, &self->trans.vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, self->trans.vbo);

    if(shader->attributes.aPosition >= 0) {
        gl->EnableVertexAttribArray(shader->attributes.aPosition);
        gl->VertexAttribPointer(shader->attributes.aPosition, 3, GL_FLOAT, GL_FALSE, sizeof(br_immvert_gl),
                                (void *)offsetof(br_immvert_gl, position));
    }

    if(shader->attributes.aUV >= 0) {
        gl->EnableVertexAttribArray(shader->attributes.aUV);
        gl->VertexAttribPointer(shader->attributes.aUV, 2, GL_FLOAT, GL_FALSE, sizeof(br_immvert_gl), (void *)offsetof(br_immvert_gl, map));
    }

    if(shader->attributes.aNormal >= 0) {
        gl->EnableVertexAttribArray(shader->attributes.aNormal);
        gl->VertexAttribPointer(shader->attributes.aNormal, 3, GL_FLOAT, GL_FALSE, sizeof(br_immvert_gl), (void *)offsetof(br_immvert_gl, normal));
    }

    if(shader->attributes.aColour >= 0) {
        gl->EnableVertexAttribArray(shader->attributes.aColour);
        gl->VertexAttribPointer(shader->attributes.aColour, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(br_immvert_gl),
                                (void *)offsetof(br_immvert_gl, colour));
    }

    gl->BindVertexArray(0);
    gl->BindBuffer(GL_ARRAY_BUFFER, 0);
}

static void RendererGLFreeImm(br_renderer *self)
{
    const GladGLContext *gl = self->gl;

    gl->DeleteVertexArrays(1, &self->trans.vao);
    gl->DeleteBuffers(1, &self->trans.vbo);
}

static br_hash SamplerInfoGLHash(const void *p)
{
    return BrHash(p, sizeof(br_sampler_info_gl));
}

static br_boolean SamplerInfoGLCompare(const void *a, const void *b)
{
    return BrMemCmp(a, b, sizeof(br_sampler_info_gl)) == 0;
}

/* BRenderModern:
 * Create a new renderer
 */
br_renderer *RendererGLAllocate(br_device *device, br_renderer_facility *facility, br_device_pixelmap *dest)
{
    br_renderer         *self;
    const GladGLContext *gl;
    br_gl_context_state *ctx;

    ASSERT(dest != NULL && ObjectDevice(dest) == device);

    if(dest->use_type != BRT_OFFSCREEN)
        return NULL;

    gl  = DevicePixelmapGLGetGLContext(dest);
    ctx = GLContextState(gl);

    self                    = BrResAllocate(facility, sizeof(*self), BR_MEMORY_OBJECT);
    self->dispatch          = &rendererDispatch;
    self->identifier        = facility->identifier;
    self->device            = device;
    self->object_list       = BrObjectListAllocate(self);
    self->gl                = gl;
    self->pixelmap          = dest;
    self->renderer_facility = facility;
    self->state_pool        = BrPoolAllocate(sizeof(state_stack), 1024, BR_MEMORY_OBJECT_DATA);

    RendererGLInitImm(self, &ctx->main_shader);

    self->sampler_pool = BrHashMapAllocate(self, SamplerInfoGLHash, SamplerInfoGLCompare);

    ObjectContainerAddFront(facility, (br_object *)self);

    StateGLInit(&self->state, self->device);

    /* BRenderModern:
     * State starts out as default
     */
    RendererStateDefault(self, BR_STATE_ALL);

    GLint alignment = 256;
    gl->GetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);

    self->uniform_buffer_offset_alignment = alignment;
    BufferRingGLInit(&self->model_ring, gl, "model", alignment, BR_GLREND_MAX_DRAWS_IN_FLIGHT, ctx->main_shader.block_binding_model,
                     sizeof(br_gl_main_data_model), GL_UNIFORM_BUFFER, ctx->quirks.orphan_model_buffers ? BUFFER_RING_GL_FLAG_ORPHAN : 0);

    self->has_begun = 0;
    return self;
}

br_boolean RendererGLSelectShadowPass(br_renderer *self, br_boolean shadow)
{
    const GladGLContext *gl = self->gl;
    br_gl_context_state *ctx = GLContextState(gl);
    br_gl_main_shader *main_shader = &ctx->main_shader;
    const br_boolean blocker_pass = shadow && self->state.current->output.shadow_blocker_pass;
    const br_boolean detail_pass = shadow && self->state.current->output.shadow_detail_pass && !blocker_pass;

    if(shadow) {
        if(self->shadow_pass_active &&
           self->shadow_blocker_pass_active == blocker_pass &&
           self->shadow_detail_pass_active == detail_pass)
            return BR_TRUE;

        if(ctx->shadow_fbo == 0 || ctx->shadow_texture == 0 || ctx->shadow_owner_texture == 0 ||
           ctx->shadow_blocker_fbo == 0 || ctx->shadow_blocker_texture == 0 ||
           ctx->shadow_shader.program == 0 || self->state.cache.scene.shadow_info.v[0] < 0.5f)
            return BR_FALSE;

        if(detail_pass && (ctx->shadow_detail_fbo == 0 || ctx->shadow_detail_texture == 0 || ctx->shadow_detail_size <= 0))
            return BR_FALSE;

#if BRENDER_LEGACY_3DMM_MODEL_ABI
        if(!self->shadow_pass_active) {
            static br_uint_32 shadow_enter_diag;
            if(shadow_enter_diag < 96) {
                BrWarning("SHADOW gl enter n=%u base_size=%u detail_size=%u shadow_fbo=%u blocker_fbo=%u detail_fbo=%u nested_detail_v237=1",
                          (unsigned)shadow_enter_diag++, (unsigned)ctx->shadow_size,
                          (unsigned)ctx->shadow_detail_size, (unsigned)ctx->shadow_fbo,
                          (unsigned)ctx->shadow_blocker_fbo, (unsigned)ctx->shadow_detail_fbo);
            }
        }
#endif

        /* Shadow shader selects base/detail projection from this scene flag. */
        self->state.cache.scene.shadow_detail_info.v[1] = detail_pass ? 1.0f : 0.0f;
        gl->BindBuffer(GL_UNIFORM_BUFFER, main_shader->ubo_scene);
        gl->BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(self->state.cache.scene), &self->state.cache.scene);
        gl->BindBufferBase(GL_UNIFORM_BUFFER, ctx->shadow_shader.block_binding_scene, main_shader->ubo_scene);

        gl->Viewport(0, 0,
                     detail_pass ? ctx->shadow_detail_size : ctx->shadow_size,
                     detail_pass ? ctx->shadow_detail_size : ctx->shadow_size);
        gl->DepthRange(0.0f, 1.0f);
        gl->UseProgram(ctx->shadow_shader.program);
        gl->Uniform1i(ctx->shadow_shader.uniform_base_depth_texture, ctx->shadow_shader.base_depth_texture_binding);
        gl->Uniform1i(ctx->shadow_shader.uniform_layer_mode, blocker_pass ? 1 : (detail_pass ? 2 : 0));

        /*
         * v239: the blocker pass depth-peels no-cast geometry against the
         * already-rendered base caster depth. A dedicated texture unit avoids
         * ever binding the base depth texture for sampling while its own FBO is
         * active.
         */
        gl->ActiveTexture(GL_TEXTURE0 + ctx->shadow_shader.base_depth_texture_binding);
        gl->BindTexture(GL_TEXTURE_2D, blocker_pass ? ctx->shadow_texture : 0);
        gl->ActiveTexture(GL_TEXTURE0);

        gl->Disable(GL_BLEND);
        gl->Disable(GL_CULL_FACE);
        gl->Disable(GL_MULTISAMPLE);
        gl->Enable(GL_DEPTH_TEST);
        gl->DepthFunc(GL_LESS);
        gl->DepthMask(GL_TRUE);
        gl->Disable(GL_SCISSOR_TEST);
        gl->Disable(GL_POLYGON_OFFSET_FILL);
        gl->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        for(br_uint_32 i = 0; i < self->state.cache.scene.num_clip_planes; ++i)
            gl->Disable(GL_CLIP_DISTANCE0 + i);

        if(!self->shadow_pass_active) {
            /* Clear every independent layer once per rendered scene. */
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
            gl->ClearDepth(1.0);
            gl->Clear(GL_DEPTH_BUFFER_BIT);
            {
                const GLuint owner_clear[4] = {0u, 0u, 0u, 0u};
                gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                gl->ClearBufferuiv(GL_COLOR, 0, owner_clear);
            }

            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_blocker_fbo);
            gl->ClearDepth(1.0);
            gl->Clear(GL_DEPTH_BUFFER_BIT);

            if(ctx->shadow_detail_fbo != 0) {
                gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_detail_fbo);
                gl->ClearDepth(1.0);
                gl->Clear(GL_DEPTH_BUFFER_BIT);
            }
        }

        if(detail_pass) {
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_detail_fbo);
            gl->ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        } else if(blocker_pass) {
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_blocker_fbo);
            gl->ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        } else {
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
            gl->ColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
        }

#if BRENDER_LEGACY_3DMM_MODEL_ABI
        if(self->shadow_pass_active &&
           (self->shadow_blocker_pass_active != blocker_pass || self->shadow_detail_pass_active != detail_pass)) {
            static br_uint_32 shadow_layer_switch_diag;
            if(shadow_layer_switch_diag < 192)
                BrWarning("SHADOW layer_switch_v237 n=%u blocker=%u detail=%u fbo=%u size=%u",
                          (unsigned)shadow_layer_switch_diag++, (unsigned)blocker_pass,
                          (unsigned)detail_pass,
                          (unsigned)(detail_pass ? ctx->shadow_detail_fbo : (blocker_pass ? ctx->shadow_blocker_fbo : ctx->shadow_fbo)),
                          (unsigned)(detail_pass ? ctx->shadow_detail_size : ctx->shadow_size));
            if(blocker_pass) {
                static br_uint_32 blocker_peel_diag;
                if(blocker_peel_diag < 24)
                    BrWarning("SHADOW blocker_depth_peel_v239 n=%u base_size=%u rule=nearest_no_cast_behind_nearest_caster",
                              (unsigned)blocker_peel_diag++, (unsigned)ctx->shadow_size);
            }
        }
#endif

        self->shadow_pass_active = BR_TRUE;
        self->shadow_blocker_pass_active = blocker_pass;
        self->shadow_detail_pass_active = detail_pass;
        return BR_TRUE;
    }

    if(!self->shadow_pass_active)
        return BR_TRUE;

    br_device_pixelmap *colour_target = self->state.current->output.colour;
    if(colour_target == NULL)
        return BR_FALSE;

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    {
        static br_uint_32 shadow_depth_diag;
        if(shadow_depth_diag < 24) {
            GLfloat samples[9];
            const GLint p[3] = {ctx->shadow_size / 4, ctx->shadow_size / 2, (ctx->shadow_size * 3) / 4};
            br_uint_32 i = 0, nonclear = 0;
            GLfloat dmin = 1.0f, dmax = 0.0f;
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
            for(int y = 0; y < 3; ++y) {
                for(int x = 0; x < 3; ++x) {
                    GLfloat d = 1.0f;
                    gl->ReadPixels(p[x], p[y], 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d);
                    samples[i++] = d;
                    if(d < dmin) dmin = d;
                    if(d > dmax) dmax = d;
                    if(d < 0.999999f) ++nonclear;
                }
            }
            BrWarning("SHADOW gl depth_samples n=%u nonclear=%u min=%.7f max=%.7f nested_detail_v237=1 detail_size=%u",
                      (unsigned)shadow_depth_diag++, (unsigned)nonclear, (double)dmin, (double)dmax,
                      (unsigned)ctx->shadow_detail_size);
        }
    }
#endif

    /* Restore colour-scene projection mode and scene UBO. */
    self->state.cache.scene.shadow_detail_info.v[1] = 0.0f;
    gl->BindBuffer(GL_UNIFORM_BUFFER, main_shader->ubo_scene);
    gl->BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(self->state.cache.scene), &self->state.cache.scene);

    gl->Disable(GL_POLYGON_OFFSET_FILL);
    gl->BindFramebuffer(GL_FRAMEBUFFER, self->state.cache.fbo);
    br_rectangle viewport = DevicePixelmapGLGetViewport(colour_target);
    gl->Viewport(viewport.x, viewport.y, viewport.w, viewport.h);
    gl->DepthRange(1.0f, 0.0f);
    gl->UseProgram(main_shader->program);
    gl->Uniform1i(main_shader->uniforms.main_texture, main_shader->main_texture_binding);
    gl->Uniform1i(main_shader->uniforms.index_texture, main_shader->index_texture_binding);
    gl->Uniform1i(main_shader->uniforms.shadow_texture, main_shader->shadow_texture_binding);
    gl->Uniform1i(main_shader->uniforms.shadow_owner_texture, main_shader->shadow_owner_texture_binding);
    gl->Uniform1i(main_shader->uniforms.shadow_blocker_texture, main_shader->shadow_blocker_texture_binding);
    gl->Uniform1i(main_shader->uniforms.shadow_detail_texture, main_shader->shadow_detail_texture_binding);
    gl->BindBufferBase(GL_UNIFORM_BUFFER, main_shader->block_binding_scene, main_shader->ubo_scene);

    gl->ActiveTexture(GL_TEXTURE2);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_texture);
    gl->ActiveTexture(GL_TEXTURE3);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_owner_texture);
    gl->ActiveTexture(GL_TEXTURE4);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_blocker_texture);
    gl->ActiveTexture(GL_TEXTURE5);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_detail_texture);
    gl->ActiveTexture(GL_TEXTURE0);

    gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    for(br_uint_32 i = 0; i < self->state.cache.scene.num_clip_planes; ++i)
        gl->Enable(GL_CLIP_DISTANCE0 + i);

    if(self->pixelmap->msaa_samples)
        gl->Enable(GL_MULTISAMPLE);
    else
        gl->Disable(GL_MULTISAMPLE);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    {
        static br_uint_32 shadow_exit_diag;
        if(shadow_exit_diag < 96)
            BrWarning("SHADOW gl exit n=%u base_size=%u detail_size=%u nested_detail_v237=1",
                      (unsigned)shadow_exit_diag++, (unsigned)ctx->shadow_size,
                      (unsigned)ctx->shadow_detail_size);
    }
#endif

    self->shadow_pass_active = BR_FALSE;
    self->shadow_blocker_pass_active = BR_FALSE;
    self->shadow_detail_pass_active = BR_FALSE;
    return BR_TRUE;
}

static void BR_CMETHOD_DECL(br_renderer_gl, sceneBegin)(br_renderer *self)
{
    const GladGLContext     *gl            = self->gl;
    br_device_pixelmap      *colour_target = NULL, *depth_target = NULL;
    const br_gl_main_shader *shader = &GLContextState(gl)->main_shader;

    self->stats.face_group_count         = 0;
    self->stats.triangles_drawn_count    = 0;
    self->stats.triangles_rendered_count = 0;
    self->stats.vertices_rendered_count  = 0;
    self->stats.opaque_draw_count        = 0;
    self->stats.transparent_draw_count   = 0;

    self->trans.next = 0;

    /* BRenderModern: First draw call, so do all the per-scene crap */
    DeviceGLCheckErrors(gl);

    if(self->state.current->valid & BR_STATE_OUTPUT) {
        colour_target = self->state.current->output.colour;
        depth_target  = self->state.current->output.depth;
    }

    if(colour_target != NULL && ObjectDevice(colour_target) != self->device) {
        BR_ERROR0("Can't render to a non-device colour pixelmap");
    }

    if(depth_target != NULL && ObjectDevice(depth_target) != self->device) {
        BR_ERROR0("Can't render to a non-device depth pixelmap");
    }

    /* BRenderModern:
     * TODO: Consider if we want to handle depth-only renders.
     */
    if(colour_target == NULL) {
        BR_ERROR("Can't render without a destination");
    }

    /* BRenderModern:
     * Cache some data before we blat our cache.
     */
    br_uint_32 last_num_clip_planes = self->state.cache.scene.num_clip_planes;

    StateGLReset(&self->state.cache);
    StateGLUpdateScene(&self->state.cache, self->state.current);

    gl->DepthRange(1.0f, 0.0f);

    gl->UseProgram(shader->program);
    gl->Uniform1i(shader->uniforms.main_texture, shader->main_texture_binding);
    gl->Uniform1i(shader->uniforms.index_texture, shader->index_texture_binding);
    gl->Uniform1i(shader->uniforms.shadow_texture, shader->shadow_texture_binding);
    gl->Uniform1i(shader->uniforms.shadow_owner_texture, shader->shadow_owner_texture_binding);
    gl->Uniform1i(shader->uniforms.shadow_blocker_texture, shader->shadow_blocker_texture_binding);
    gl->Uniform1i(shader->uniforms.shadow_detail_texture, shader->shadow_detail_texture_binding);

    br_gl_context_state *ctx = GLContextState(gl);
    if(ctx->shadow_fbo == 0 || ctx->shadow_texture == 0 || ctx->shadow_owner_texture == 0 ||
       ctx->shadow_blocker_fbo == 0 || ctx->shadow_blocker_texture == 0 ||
       ctx->shadow_shader.program == 0)
        self->state.cache.scene.shadow_info.v[0] = 0.0f;

    self->state.cache.scene.shadow_detail_info.v[0] =
        (ctx->shadow_detail_fbo != 0 && ctx->shadow_detail_texture != 0 && ctx->shadow_detail_size > 0) ? 1.0f : 0.0f;
    self->state.cache.scene.shadow_detail_info.v[1] = 0.0f;

    gl->BindFramebuffer(GL_FRAMEBUFFER, self->state.cache.fbo);
    gl->BindBufferBase(GL_UNIFORM_BUFFER, shader->block_binding_scene, shader->ubo_scene);
    gl->BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(self->state.cache.scene), &self->state.cache.scene);

    gl->ActiveTexture(GL_TEXTURE2);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_texture);
    gl->ActiveTexture(GL_TEXTURE3);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_owner_texture);
    gl->ActiveTexture(GL_TEXTURE4);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_blocker_texture);
    gl->ActiveTexture(GL_TEXTURE5);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_detail_texture);
    gl->ActiveTexture(GL_TEXTURE0);
    self->shadow_pass_active = BR_FALSE;
    self->shadow_blocker_pass_active = BR_FALSE;
    self->shadow_detail_pass_active = BR_FALSE;

    br_rectangle viewport = DevicePixelmapGLGetViewport(colour_target);
    gl->Viewport(viewport.x, viewport.y, viewport.w, viewport.h);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    if(self->state.cache.scene.shadow_info.v[0] >= 0.5f) {
        static br_uint_32 shadow_scene_diag;
        if(shadow_scene_diag < 192) {
            const int li = (int)(self->state.cache.scene.shadow_info.v[1] + 0.5f);
            const br_vector4_f *lp = li >= 0 && li < MAX_STATE_LIGHTS ? &self->state.cache.scene.light_positions[li] : NULL;
            const br_vector4_f *ld = li >= 0 && li < MAX_STATE_LIGHTS ? &self->state.cache.scene.light_directions[li] : NULL;
            const br_vector4_f *su = &self->state.cache.scene.shadow_world_up_view;
            const br_vector4_f *df = &self->state.cache.scene.shadow_detail_fit;
            const br_matrix34 *v2l = &self->state.cache.shadow_view_to_light;
            const br_float tan_half = self->state.cache.scene.shadow_info.v[2];
            const br_float gain_u = su->v[2] > 0.0f ? tan_half / su->v[2] : 1.0f;
            const br_float gain_v = su->v[3] > 0.0f ? tan_half / su->v[3] : 1.0f;
            const br_float physical_gain =
                (ctx->shadow_size > 0 && ctx->shadow_detail_size > 0) ?
                (br_float)ctx->shadow_detail_size / (br_float)ctx->shadow_size : 1.0f;
            const br_float detail_gain_u =
                df->v[2] > 0.0f ? physical_gain * su->v[2] / df->v[2] : 1.0f;
            const br_float detail_gain_v =
                df->v[3] > 0.0f ? physical_gain * su->v[3] / df->v[3] : 1.0f;
            BrWarning("SHADOW scene n=%u pass=%s li=%d pos=%.3g,%.3g,%.3g dir=%.3g,%.3g,%.3g fit=(%.7g,%.7g,%.7g,%.7g) fit_gain=(%.3fx,%.3fx) detail_fit=(%.7g,%.7g,%.7g,%.7g) detail_size=%u detail_effective_gain=(%.3fx,%.3fx) near=%.3g far=%.3g adaptive_detail_v254=1 nested_detail_v237=1 fitted_projection_v228=1 receiver_plane_v229=1 horizontal_height_gate_v230=1 center_gate_v230=1 texel_center_ray_v231=1 horizontal_silhouette_gate_v231=7 light_local_projection_v218=1 contact_bias_v222=0 polygon_offset_v222=0 geometric_receiver_plane_v271=1 detail_handoff_blend_v271=1",
                      (unsigned)shadow_scene_diag++, self->state.current->output.shadow_pass ? "depth" : "colour", li,
                      lp ? (double)lp->v[0] : 0.0, lp ? (double)lp->v[1] : 0.0, lp ? (double)lp->v[2] : 0.0,
                      ld ? (double)ld->v[0] : 0.0, ld ? (double)ld->v[1] : 0.0, ld ? (double)ld->v[2] : 0.0,
                      (double)su->v[0], (double)su->v[1], (double)su->v[2], (double)su->v[3],
                      (double)gain_u, (double)gain_v,
                      (double)df->v[0], (double)df->v[1],
                      (double)df->v[2], (double)df->v[3],
                      (unsigned)ctx->shadow_detail_size,
                      (double)detail_gain_u, (double)detail_gain_v,
                      (double)self->state.cache.scene.shadow_info.v[3], (double)self->state.cache.scene.shadow_info2.v[0],
                      (double)BrScalarToFloat(v2l->m[0][0]), (double)BrScalarToFloat(v2l->m[0][1]), (double)BrScalarToFloat(v2l->m[0][2]),
                      (double)BrScalarToFloat(v2l->m[1][0]), (double)BrScalarToFloat(v2l->m[1][1]), (double)BrScalarToFloat(v2l->m[1][2]),
                      (double)BrScalarToFloat(v2l->m[2][0]), (double)BrScalarToFloat(v2l->m[2][1]), (double)BrScalarToFloat(v2l->m[2][2]),
                      (double)BrScalarToFloat(v2l->m[3][0]), (double)BrScalarToFloat(v2l->m[3][1]), (double)BrScalarToFloat(v2l->m[3][2]));
        }
    }
#endif

    if(self->pixelmap->msaa_samples)
        gl->Enable(GL_MULTISAMPLE);

    /* BRenderModern:
     * Now enable our clip planes.
     */
    br_uint_32 current_num_clip_planes = self->state.cache.scene.num_clip_planes;

    if(last_num_clip_planes > current_num_clip_planes) {
        for(br_uint_32 i = current_num_clip_planes; i < last_num_clip_planes; ++i) {
            gl->Disable(GL_CLIP_DISTANCE0 + i);
        }
    }

    for(br_uint_32 i = last_num_clip_planes; i < current_num_clip_planes; ++i) {
        gl->Enable(GL_CLIP_DISTANCE0 + i);
    }

    BufferRingGLBegin(&self->model_ring);

    self->has_begun = 1;
}

void BR_CMETHOD_DECL(br_renderer_gl, sceneEnd)(br_renderer *self)
{
    const GladGLContext *gl = self->gl;

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    {
        static br_uint_32 diag_scene_count;
        if(diag_scene_count < 240) {
            BrWarning("3DMM glrend scene-end n=%u groups=%u triangles_drawn=%u triangles_rendered=%u vertices=%u opaque_draws=%u transparent_draws=%u trans_next=%u",
                      (unsigned)diag_scene_count, (unsigned)self->stats.face_group_count,
                      (unsigned)self->stats.triangles_drawn_count, (unsigned)self->stats.triangles_rendered_count,
                      (unsigned)self->stats.vertices_rendered_count, (unsigned)self->stats.opaque_draw_count,
                      (unsigned)self->stats.transparent_draw_count, (unsigned)self->trans.next);
            ++diag_scene_count;
        }
    }
#endif

    (void)RendererGLSelectShadowPass(self, BR_FALSE);
    BufferRingGLEnd(&self->model_ring);

    gl->Disable(GL_CULL_FACE);
    gl->Disable(GL_DEPTH_TEST);
    gl->Disable(GL_BLEND);
    gl->Disable(GL_MULTISAMPLE);
    gl->Disable(GL_POLYGON_OFFSET_FILL);

    gl->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    gl->BindTexture(GL_TEXTURE_2D, 0);
    gl->BindBuffer(GL_UNIFORM_BUFFER, 0);
    gl->BindVertexArray(0);

    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->UseProgram(0);
    self->has_begun = 0;

    BrPoolEmpty(self->state_pool);

    self->trans.next = 0;
}

static int RendererGLDeleteSampler(const void *key, void *value, br_hash hash, void *user)
{
    const GladGLContext *gl = user;

    GLuint sampler = (GLuint)(br_uintptr_t)value;
    gl->DeleteSamplers(1, &sampler);
    return 0;
}

static void BR_CMETHOD_DECL(br_renderer_gl, free)(br_object *_self)
{
    br_renderer *self = (br_renderer *)_self;

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    /*
     * v245: 3DMM/Actor Studio can replace or mutate a legacy model in a way
     * that strands an older br_geometry_stored in the renderer facility.
     * BrV1dbRendererEnd() clears every currently registered model first, so
     * anything still present here is orphaned GPU geometry.  Free it now,
     * while this renderer's GL dispatch/context is still valid.  If it is
     * allowed to survive until device/facility teardown, FreeRenderBuffers()
     * has already destroyed the GL context and geometryStoredFree() will call
     * through a stale self->gl pointer.
     */
    {
        br_object_container *facility = (br_object_container *)self->renderer_facility;
        br_int_32 count = 0;

        if(ObjectContainerCount(facility, &count, BRT_GEOMETRY_STORED, NULL, NULL) == BRE_OK && count > 0) {
            BrWarning("3DMM v245 renderer shutdown freeing %d orphan stored geometries before GL context teardown", (int)count);
            BrObjectContainerFree(facility, BRT_GEOMETRY_STORED, NULL, NULL);
        }
    }
#endif

    RendererGLFreeImm(self);

    BrHashMapEnumerate(self->sampler_pool, RendererGLDeleteSampler, (void *)self->gl);

    BrPoolFree(self->state_pool);

    ObjectContainerRemove(self->renderer_facility, (br_object *)self);

    BrObjectContainerFree((br_object_container *)self, BR_NULL_TOKEN, NULL, NULL);

    BufferRingGLFini(&self->model_ring);

    BrResFreeNoCallback(self);
}

static const char *BR_CMETHOD_DECL(br_renderer_gl, identifier)(br_object *self)
{
    return ((br_renderer *)self)->identifier;
}

static br_token BR_CMETHOD_DECL(br_renderer_gl, type)(br_object *self)
{
    return BRT_RENDERER;
}

static br_boolean BR_CMETHOD_DECL(br_renderer_gl, isType)(br_object *self, br_token t)
{
    return (t == BRT_RENDERER) || (t == BRT_OBJECT);
}

static struct br_device *BR_CMETHOD_DECL(br_renderer_gl, device)(br_object *self)
{
    return ((br_renderer *)self)->device;
}

static br_size_t BR_CMETHOD_DECL(br_renderer_gl, space)(br_object *self)
{
    return sizeof(br_renderer);
}

static br_tv_template *BR_CMETHOD_DECL(br_renderer_gl, templateQuery)(br_object *_self)
{
    br_renderer *self = (br_renderer *)_self;

    if(self->device->templates.rendererTemplate == NULL) {
        self->device->templates.rendererTemplate = BrTVTemplateAllocate(self->device, rendererTemplateEntries, BR_ASIZE(rendererTemplateEntries));
    }

    return self->device->templates.rendererTemplate;
}

static void *BR_CMETHOD_DECL(br_renderer_gl, listQuery)(br_object_container *self)
{
    return ((br_renderer *)self)->object_list;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, validDestination)(br_renderer *self, br_boolean *bp, br_object *h)
{
    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, stateStoredNew)(br_renderer *self, br_renderer_state_stored **pss, br_uint_32 mask,
                                                                br_token_value *tv)
{
    br_renderer_state_stored *ss;

    if((ss = RendererStateStoredGLAllocate(self, self->state.current, mask, tv)) == NULL)
        return BRE_FAIL;

    *pss = ss;
    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, stateStoredAvail)(br_renderer *self, br_int_32 *psize, br_uint_32 mask, br_token_value *tv)
{
    return BRE_FAIL;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, bufferStoredNew)(br_renderer *self, br_buffer_stored **psm, br_token use,
                                                                 br_device_pixelmap *pm, br_token_value *tv)
{
    br_buffer_stored *sm;

    if((sm = BufferStoredGLAllocate(self, use, pm, tv)) == NULL)
        return BRE_FAIL;

    *psm = sm;
    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, bufferStoredAvail)(br_renderer *self, br_int_32 *space, br_token use, br_token_value *tv)
{
    (void)self;
    (void)space;
    (void)use;
    (void)tv;
    return BRE_FAIL;
}

/* BRenderModern:
 * Setting current state
 */
br_error BR_CMETHOD_DECL(br_renderer_gl, partSet)(br_renderer *self, br_token part, br_int_32 index, br_token t, br_value value)
{
    br_error               r;
    br_uint_32             m;
    struct br_tv_template *tp;

    if((tp = StateGLGetStateTemplate(&self->state, part, index)) == NULL)
        return BRE_FAIL;

    m = 0;
    r = BrTokenValueSet(self->state.current, &m, t, value, tp);
    if(m)
        StateGLTemplateActions(&self->state, m);

    return r;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, partSetMany)(br_renderer *self, br_token part, br_int_32 index, br_token_value *tv, br_int_32 *pcount)
{
    br_error        r;
    br_uint_32      m;
    br_tv_template *tp;

    if((tp = StateGLGetStateTemplate(&self->state, part, index)) == NULL)
        return BRE_FAIL;

    m = 0;
    r = BrTokenValueSetMany(self->state.current, pcount, &m, tv, tp);
    if(m)
        StateGLTemplateActions(&self->state, m);

    return r;
}

/* BRenderModern:
 * Reading current state
 */
static br_error BR_CMETHOD_DECL(br_renderer_gl, partQuery)(br_renderer *self, br_token part, br_int_32 index, void *pvalue, br_token t)
{
    br_tv_template *tp;

    if((tp = StateGLGetStateTemplate(&self->state, part, index)) == NULL)
        return BRE_FAIL;

    return BrTokenValueQuery(pvalue, NULL, 0, t, self->state.current, tp);
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, partQueryBuffer)(br_renderer *self, br_token part, br_int_32 index, void *pvalue,
                                                                 void *buffer, br_size_t buffer_size, br_token t)
{
    br_tv_template *tp;

    if((tp = StateGLGetStateTemplate(&self->state, part, index)) == NULL)
        return BRE_FAIL;

    return BrTokenValueQuery(pvalue, buffer, buffer_size, t, self->state.current, tp);
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, partQueryMany)(br_renderer *self, br_token part, br_int_32 index, br_token_value *tv,
                                                               void *extra, br_size_t extra_size, br_int_32 *pcount)
{
    br_tv_template *tp;

    if((tp = StateGLGetStateTemplate(&self->state, part, index)) == NULL)
        return BRE_FAIL;

    return BrTokenValueQueryMany(tv, extra, extra_size, pcount, self->state.current, tp);
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, partQueryManySize)(br_renderer *self, br_token part, br_int_32 index,
                                                                   br_size_t *pextra_size, br_token_value *tv)
{
    br_tv_template *tp;

    if((tp = StateGLGetStateTemplate(&self->state, part, index)) == NULL)
        return BRE_FAIL;

    return BrTokenValueQueryManySize(pextra_size, tv, self->state.current, tp);
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, partQueryAll)(br_renderer *self, br_token part, br_int_32 index, br_token_value *buffer,
                                                              br_size_t buffer_size)
{
    br_tv_template *tp;

    if((tp = StateGLGetStateTemplate(&self->state, part, index)) == NULL)
        return BRE_FAIL;

    return BrTokenValueQueryAll(buffer, buffer_size, self->state.current, tp);
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, partQueryAllSize)(br_renderer *self, br_token part, br_int_32 index, br_size_t *psize)
{
    br_tv_template *tp;

    if((tp = StateGLGetStateTemplate(&self->state, part, index)) == NULL)
        return BRE_FAIL;

    return BrTokenValueQueryAllSize(psize, self->state.current, tp);
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, partIndexQuery)(br_renderer *self, br_token part, br_int_32 *pnindex)
{
    (void)self;

    if(pnindex == NULL)
        return BRE_FAIL;

    switch(part) {
        /* BRenderModern: Renderer states. */
        case BRT_CULL:
        case BRT_SURFACE:
        case BRT_MATRIX:
        case BRT_ENABLE:
        case BRT_BOUNDS:
        case BRT_HIDDEN_SURFACE:
            *pnindex = 1;
            return BRE_OK;

        case BRT_LIGHT:
            *pnindex = MAX_STATE_LIGHTS;
            return BRE_OK;

        case BRT_CLIP:
            *pnindex = MAX_STATE_CLIP_PLANES;
            return BRE_OK;

        /* BRenderModern: Primitive states. */
        case BRT_OUTPUT:
        case BRT_PRIMITIVE:
            *pnindex = 1;
            return BRE_OK;

        default:
            break;
    }

    return BRE_FAIL;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, commandModeSet)(br_renderer *self, br_token mode)
{
    return BRE_FAIL;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, commandModeQuery)(br_renderer *self, br_token *mode)
{
    return BRE_FAIL;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, commandModeDefault)(br_renderer *self)
{
    return BRE_FAIL;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, commandModePush)(br_renderer *self)
{
    return BRE_FAIL;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, commandModePop)(br_renderer *self)
{
    return BRE_FAIL;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, modelMul)(br_renderer *self, br_matrix34 *m)
{
    br_matrix34 om = self->state.current->matrix.model_to_view;

    BrMatrix34Mul(&self->state.current->matrix.model_to_view, m, &om);

    self->state.current->matrix.model_to_view_hint = BRT_NONE;

    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, modelPopPushMul)(br_renderer *self, br_matrix34 *m)
{
    if(self->state.top == 0)
        return BRE_UNDERFLOW;

    BrMatrix34Mul(&self->state.current->matrix.model_to_view, m, &self->state.stack[0].matrix.model_to_view);

    self->state.current->matrix.model_to_view_hint = BRT_NONE;

    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, modelInvert)(br_renderer *self)
{
    br_matrix34 old;

    BrMatrix34Copy(&old, &self->state.current->matrix.model_to_view);

    if(self->state.current->matrix.model_to_view_hint == BRT_LENGTH_PRESERVING)
        BrMatrix34LPInverse(&self->state.current->matrix.model_to_view, &old);
    else
        BrMatrix34Inverse(&self->state.current->matrix.model_to_view, &old);

    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, statePush)(br_renderer *self, br_uint_32 mask)
{
    return StateGLPush(&self->state, mask) ? BRE_OK : BRE_OVERFLOW;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, statePop)(br_renderer *self, br_uint_32 mask)
{
    return StateGLPop(&self->state, mask) ? BRE_OK : BRE_OVERFLOW;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, stateSave)(br_renderer *self, br_renderer_state_stored *save, br_uint_32 mask)
{
    StateGLCopy(&save->state, self->state.current, mask);
    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, stateRestore)(br_renderer *self, br_renderer_state_stored *save, br_uint_32 mask)
{
    StateGLCopy(self->state.current, &save->state, mask);
    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, stateDefault)(br_renderer *self, br_uint_32 mask)
{
    StateGLDefault(&self->state, mask);
    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, stateMask)(br_renderer *self, br_uint_32 *mask, const br_token *parts, br_size_t n_parts)
{
    br_uint_32 m;

    (void)self;

    if(mask == NULL)
        return BRE_FAIL;

    m = 0;
    for(br_size_t i = 0; i < n_parts; i++) {
        switch(parts[i]) {
            case BRT_SURFACE:
                m |= MASK_STATE_SURFACE;
                break;

            case BRT_MATRIX:
                m |= MASK_STATE_MATRIX;
                break;

            case BRT_ENABLE:
                m |= MASK_STATE_ENABLE;
                break;

            case BRT_LIGHT:
                m |= MASK_STATE_LIGHT;
                break;

            case BRT_CLIP:
                m |= MASK_STATE_CLIP;
                break;

            case BRT_BOUNDS:
                m |= MASK_STATE_BOUNDS;
                break;

            case BRT_CULL:
                m |= MASK_STATE_CULL;
                break;

            case BRT_OUTPUT:
                m |= MASK_STATE_OUTPUT;
                break;

            case BRT_PRIMITIVE:
                m |= MASK_STATE_PRIMITIVE;
                break;

            default:
                break;
        }
    }

    *mask = m;

    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, boundsTest)(br_renderer *self, br_token *r, br_bounds3 *bounds)
{
    // BRenderModern: FIXME: Should probably cache this.
    br_matrix4 m2s;
    BrMatrix4Mul34(&m2s, &self->state.current->matrix.model_to_view, &self->state.current->matrix.view_to_screen);
    *r = GLOnScreenCheck(&m2s, bounds);
    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, flush)(br_renderer *self, br_boolean wait)
{
    const GladGLContext *gl = self->gl;

    (void)self;
    gl->Flush();

    if(wait)
        gl->Finish();

    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, synchronise)(br_renderer *self, br_token sync_type, br_boolean block)
{
    return BRE_UNSUPPORTED;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, frameBegin)(br_renderer *self)
{
    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, frameEnd)(br_renderer *self)
{
    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, focusLossBegin)(br_renderer *self)
{
    return BRE_OK;
}

static br_error BR_CMETHOD_DECL(br_renderer_gl, focusLossEnd)(br_renderer *self)
{
    return BRE_OK;
}

/* BRenderModern:
 * Default dispatch table for renderer
 */
static const struct br_renderer_dispatch rendererDispatch = {
    .__reserved0           = NULL,
    .__reserved1           = NULL,
    .__reserved2           = NULL,
    .__reserved3           = NULL,
    ._free                 = BR_CMETHOD(br_renderer_gl, free),
    ._identifier           = BR_CMETHOD(br_renderer_gl, identifier),
    ._type                 = BR_CMETHOD(br_renderer_gl, type),
    ._isType               = BR_CMETHOD(br_renderer_gl, isType),
    ._device               = BR_CMETHOD(br_renderer_gl, device),
    ._space                = BR_CMETHOD(br_renderer_gl, space),
    ._templateQuery        = BR_CMETHOD(br_renderer_gl, templateQuery),
    ._query                = BR_CMETHOD(br_object, query),
    ._queryBuffer          = BR_CMETHOD(br_object, queryBuffer),
    ._queryMany            = BR_CMETHOD(br_object, queryMany),
    ._queryManySize        = BR_CMETHOD(br_object, queryManySize),
    ._queryAll             = BR_CMETHOD(br_object, queryAll),
    ._queryAllSize         = BR_CMETHOD(br_object, queryAllSize),
    ._listQuery            = BR_CMETHOD(br_renderer_gl, listQuery),
    ._tokensMatchBegin     = BR_CMETHOD(br_object_container, tokensMatchBegin),
    ._tokensMatch          = BR_CMETHOD(br_object_container, tokensMatch),
    ._tokensMatchEnd       = BR_CMETHOD(br_object_container, tokensMatchEnd),
    ._tokensMatchInfoQuery = BR_CMETHOD_REF(br_object_container, tokensMatchInfoQuery),
    ._addFront             = BR_CMETHOD(br_object_container, addFront),
    ._removeFront          = BR_CMETHOD(br_object_container, removeFront),
    ._remove               = BR_CMETHOD(br_object_container, remove),
    ._find                 = BR_CMETHOD(br_object_container, find),
    ._findMany             = BR_CMETHOD(br_object_container, findMany),
    ._count                = BR_CMETHOD(br_object_container, count),

    ._validDestination      = BR_CMETHOD(br_renderer_gl, validDestination),
    ._stateStoredNew        = BR_CMETHOD(br_renderer_gl, stateStoredNew),
    ._stateStoredAvail      = BR_CMETHOD(br_renderer_gl, stateStoredAvail),
    ._bufferStoredNew       = BR_CMETHOD(br_renderer_gl, bufferStoredNew),
    ._bufferStoredAvail     = BR_CMETHOD(br_renderer_gl, bufferStoredAvail),
    ._partSet               = BR_CMETHOD(br_renderer_gl, partSet),
    ._partSetMany           = BR_CMETHOD(br_renderer_gl, partSetMany),
    ._partQuery             = BR_CMETHOD(br_renderer_gl, partQuery),
    ._partQueryBuffer       = BR_CMETHOD(br_renderer_gl, partQueryBuffer),
    ._partQueryMany         = BR_CMETHOD(br_renderer_gl, partQueryMany),
    ._partQueryManySize     = BR_CMETHOD(br_renderer_gl, partQueryManySize),
    ._partQueryAll          = BR_CMETHOD(br_renderer_gl, partQueryAll),
    ._partQueryAllSize      = BR_CMETHOD(br_renderer_gl, partQueryAllSize),
    ._partIndexQuery        = BR_CMETHOD(br_renderer_gl, partIndexQuery),
    ._modelMul              = BR_CMETHOD(br_renderer_gl, modelMul),
    ._modelPopPushMul       = BR_CMETHOD(br_renderer_gl, modelPopPushMul),
    ._modelInvert           = BR_CMETHOD(br_renderer_gl, modelInvert),
    ._statePush             = BR_CMETHOD(br_renderer_gl, statePush),
    ._statePop              = BR_CMETHOD(br_renderer_gl, statePop),
    ._stateSave             = BR_CMETHOD(br_renderer_gl, stateSave),
    ._stateRestore          = BR_CMETHOD(br_renderer_gl, stateRestore),
    ._stateMask             = BR_CMETHOD(br_renderer_gl, stateMask),
    ._stateDefault          = BR_CMETHOD(br_renderer_gl, stateDefault),
    ._boundsTest            = BR_CMETHOD(br_renderer_gl, boundsTest),
    ._commandModeSet        = BR_CMETHOD(br_renderer_gl, commandModeSet),
    ._commandModeQuery      = BR_CMETHOD(br_renderer_gl, commandModeQuery),
    ._commandModeDefault    = BR_CMETHOD(br_renderer_gl, commandModeDefault),
    ._commandModePush       = BR_CMETHOD(br_renderer_gl, commandModePush),
    ._commandModePop        = BR_CMETHOD(br_renderer_gl, commandModePop),
    ._flush                 = BR_CMETHOD(br_renderer_gl, flush),
    ._synchronise           = BR_CMETHOD(br_renderer_gl, synchronise),
    ._frameBegin            = BR_CMETHOD(br_renderer_gl, frameBegin),
    ._frameEnd              = BR_CMETHOD(br_renderer_gl, frameEnd),
    ._focusLossBegin        = BR_CMETHOD(br_renderer_gl, focusLossBegin),
    ._focusLossEnd          = BR_CMETHOD(br_renderer_gl, focusLossEnd),
    ._sceneBegin            = BR_CMETHOD(br_renderer_gl, sceneBegin),
    ._sceneEnd              = BR_CMETHOD(br_renderer_gl, sceneEnd),
};

state_stack *RendererGLAllocState(br_renderer *self, const state_stack *tpl, br_uint_32 refs)
{
    state_stack *state = BrPoolBlockAllocate(self->state_pool);
    *state             = *tpl;
    state->num_refs    = refs;
    return state;
}

void RendererGLUnrefState(br_renderer *self, state_stack *state)
{
    --state->num_refs;

    if(state->num_refs == 0)
        BrPoolBlockFree(self->state_pool, state);
}

br_int_32 RendererGLNextImmTri(br_renderer *self, struct v11group *group, br_vector3_u16 fp)
{
    br_size_t base = self->trans.next;

    if(base + 3 >= BR_ASIZE(self->trans.pool))
        return -1;

    br_immvert_gl *v0 = self->trans.pool + self->trans.next++;
    br_immvert_gl *v1 = self->trans.pool + self->trans.next++;
    br_immvert_gl *v2 = self->trans.pool + self->trans.next++;

    BrGLVector3ToFloat(&v0->position, group->position + fp.v[0]);
    BrGLVector3ToFloat(&v0->normal, group->normal + fp.v[0]);
    BrGLVector2ToFloat(&v0->map, group->map + fp.v[0]);
    v0->colour = group->vertex_colours[fp.v[0]];

    BrGLVector3ToFloat(&v1->position, group->position + fp.v[1]);
    BrGLVector3ToFloat(&v1->normal, group->normal + fp.v[1]);
    BrGLVector2ToFloat(&v1->map, group->map + fp.v[1]);
    v1->colour = group->vertex_colours[fp.v[1]];

    BrGLVector3ToFloat(&v2->position, group->position + fp.v[2]);
    BrGLVector3ToFloat(&v2->normal, group->normal + fp.v[2]);
    BrGLVector2ToFloat(&v2->map, group->map + fp.v[2]);
    v2->colour = group->vertex_colours[fp.v[2]];

    return (br_int_32)base;
}

GLuint RendererGLGetSampler(br_renderer *self, const br_sampler_info_gl *info)
{
    const GladGLContext       *gl  = self->gl;
    const br_gl_context_state *ctx = GLContextState(gl);
    void                      *raw_sampler;
    br_sampler_info_gl        *key;
    GLuint                     sampler;

    raw_sampler = BrHashMapFindByHash(self->sampler_pool, SamplerInfoGLHash(info));
    if(raw_sampler != NULL)
        return (GLuint)(br_uintptr_t)raw_sampler;

    gl->GenSamplers(1, &sampler);

    gl->SamplerParameteri(sampler, GL_TEXTURE_WRAP_S, info->wrap_s);
    gl->SamplerParameteri(sampler, GL_TEXTURE_WRAP_T, info->wrap_t);
    gl->SamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, info->filter_min);
    gl->SamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, info->filter_mag);

    if(!ctx->quirks.disable_anisotropic_filtering) {
        if(gl->EXT_texture_filter_anisotropic && info->filter_min != GL_NEAREST && info->filter_mag != GL_NEAREST) {
            gl->SamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY_EXT, GLContextState(gl)->limits.max_anisotropy);
        }
    }

    key  = BrResAllocate(self->sampler_pool, sizeof(br_sampler_info_gl), BR_MEMORY_DRIVER);
    *key = *info;

    BrHashMapInsert(self->sampler_pool, key, (void *)(br_uintptr_t)sampler);

    return sampler;
}
