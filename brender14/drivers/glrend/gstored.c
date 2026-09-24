/* BRenderModern:
 * Geometry format for version 1 models
 */
#include <string.h>
#include "drv.h"
#include "state.h"
#include "brassert.h"
#include "formats.h"

/* BRenderModern:
 * Default dispatch table for geometry type (defined at and of file)
 */
static const struct br_geometry_stored_dispatch geometryStoredDispatch;

/* BRenderModern:
 * Geometry format info. template
 */
#define F(f) offsetof(br_geometry_stored, f)

static br_tv_template_entry templateEntries[] = {
    {BRT(IDENTIFIER_CSTR),   F(identifier), BRTV_QUERY | BRTV_ALL, BRTV_CONV_COPY},
    {BRT(GEOMETRY_V1_MODEL), F(gv1model),   BRTV_QUERY | BRTV_ALL, BRTV_CONV_COPY},
    {BRT(SHARED_B),          F(shared),     BRTV_QUERY | BRTV_ALL, BRTV_CONV_COPY},
};
#undef F

static GLuint create_vao(const GladGLContext *gl, const br_gl_main_shader *shader, GLuint vbo, GLuint ibo)
{
    GLuint vao;
    gl->GenVertexArrays(1, &vao);
    gl->BindVertexArray(vao);

    gl->BindBuffer(GL_ARRAY_BUFFER, vbo);

    if(shader->attributes.aPosition >= 0) {
        gl->EnableVertexAttribArray(shader->attributes.aPosition);
        gl->VertexAttribPointer(shader->attributes.aPosition, 3, GL_FLOAT, GL_FALSE, sizeof(gl_vertex_f), (void *)offsetof(gl_vertex_f, p));
    }

    if(shader->attributes.aUV >= 0) {
        gl->EnableVertexAttribArray(shader->attributes.aUV);
        gl->VertexAttribPointer(shader->attributes.aUV, 2, GL_FLOAT, GL_FALSE, sizeof(gl_vertex_f), (void *)offsetof(gl_vertex_f, map));
    }

    if(shader->attributes.aNormal >= 0) {
        gl->EnableVertexAttribArray(shader->attributes.aNormal);
        gl->VertexAttribPointer(shader->attributes.aNormal, 3, GL_FLOAT, GL_FALSE, sizeof(gl_vertex_f), (void *)offsetof(gl_vertex_f, n));
    }

    if(shader->attributes.aColour >= 0) {
        gl->EnableVertexAttribArray(shader->attributes.aColour);
        gl->VertexAttribPointer(shader->attributes.aColour, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(gl_vertex_f), (void *)offsetof(gl_vertex_f, c));
    }

    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    gl->BindVertexArray(0);
    return vao;
}

static GLuint build_vbo(const GladGLContext *gl, const struct v11model *model, size_t total_vertices, gl_groupinfo *groups)
{
    /* BRenderModern: Collate and upload the vertex data. */
    gl_vertex_f *vtx      = (gl_vertex_f *)BrScratchAllocate(total_vertices * sizeof(gl_vertex_f));
    gl_vertex_f *nextVtx  = vtx;
    GLsizei      v_offset = 0;
    GLuint       buf;

    for(br_uint_16 i = 0; i < model->ngroups; ++i) {
        const struct v11group *gp = model->groups + i;

        groups[i].vertex_offset = v_offset;

        for(br_uint_16 v = 0; v < gp->nvertices; ++v, ++nextVtx) {
            BrGLVector3ToFloat(&nextVtx->p, gp->position + v);
            BrGLVector2ToFloat(&nextVtx->map, gp->map + v);
            BrGLVector3ToFloat(&nextVtx->n, gp->normal + v);
            nextVtx->c[0] = BR_RED(gp->vertex_colours[v]);
            nextVtx->c[1] = BR_GRN(gp->vertex_colours[v]);
            nextVtx->c[2] = BR_BLU(gp->vertex_colours[v]);
            nextVtx->c[3] = 255;
        }

        v_offset += (GLsizei)(gp->nvertices * sizeof(gl_vertex_f));
    }

    gl->GenBuffers(1, &buf);
    gl->BindBuffer(GL_ARRAY_BUFFER, buf);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(total_vertices * sizeof(gl_vertex_f)), vtx, GL_STATIC_DRAW);
    BrScratchFree(vtx);
    return buf;
}

static GLuint build_ibo(const GladGLContext *gl, const struct v11model *model, size_t total_faces, gl_groupinfo *groups)
{
    size_t total_edges = 0;
    for(br_uint_16 i = 0; i < model->ngroups; ++i) {
        const struct v11group *gp = model->groups + i;
        if(gp->edges != NULL && gp->nedges > 1)
            total_edges += gp->nedges - 1;
    }

    br_uint_16 *idx = (br_uint_16 *)BrScratchAllocate((total_faces * 3 + total_edges * 2) * sizeof(br_uint_16));
    GLuint      buf;

    br_uint_16 *nextIdx      = idx;
    br_uint_16  vertex_base  = 0;
    br_size_t   index_offset = 0;

    for(br_uint_16 i = 0; i < model->ngroups; ++i) {
        const struct v11group *gp = model->groups + i;

        groups[i].count  = (GLsizei)gp->nfaces * 3;
        groups[i].offset = (void *)(br_uintptr_t)index_offset;
        groups[i].group  = model->groups + i;

        for(br_uint_16 f = 0; f < gp->nfaces; ++f) {
            const br_vector3_u16 *fp = gp->vertex_numbers + f;
            *nextIdx++               = fp->v[0] + vertex_base;
            *nextIdx++               = fp->v[1] + vertex_base;
            *nextIdx++               = fp->v[2] + vertex_base;
        }
        index_offset += (br_size_t)groups[i].count * sizeof(br_uint_16);

        groups[i].line_count  = 0;
        groups[i].line_offset = (void *)(br_uintptr_t)index_offset;

        if(gp->edges != NULL && gp->nedges > 1) {
            /*
             * build_ibo() already owns BRender's single global scratchpad in
             * idx. A second BrScratchAllocate() here fatals immediately with
             * "Scratchpad not available" while BrRendererBegin() stores the
             * first model. This short-lived edge bitmap is independent of
             * idx, so use the ordinary allocator instead of nesting scratch.
             */
            br_uint_8 *seen = BrMemAllocate(gp->nedges * sizeof(*seen), BR_MEMORY_SCRATCH);
            memset(seen, 0, gp->nedges * sizeof(*seen));
            seen[0] = 1; /* BRender reserves edge 0 for internal/coplanar edges. */

            for(br_uint_16 f = 0; f < gp->nfaces; ++f) {
                const br_vector3_u16 *fp = gp->vertex_numbers + f;
                const br_vector3_u16 *ep = gp->edges + f;

                for(br_uint_16 e = 0; e < 3; ++e) {
                    const br_uint_16 edge_id = ep->v[e];
                    if(edge_id >= gp->nedges || seen[edge_id])
                        continue;

                    switch(e) {
                        case 0:
                            *nextIdx++ = fp->v[0] + vertex_base;
                            *nextIdx++ = fp->v[1] + vertex_base;
                            break;
                        case 1:
                            *nextIdx++ = fp->v[1] + vertex_base;
                            *nextIdx++ = fp->v[2] + vertex_base;
                            break;
                        default:
                            *nextIdx++ = fp->v[2] + vertex_base;
                            *nextIdx++ = fp->v[0] + vertex_base;
                            break;
                    }
                    seen[edge_id] = 1;
                    groups[i].line_count += 2;
                }
            }
            BrMemFree(seen);
        }

        index_offset += (br_size_t)groups[i].line_count * sizeof(br_uint_16);
        vertex_base += gp->nvertices;
    }

    gl->GenBuffers(1, &buf);
    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
    gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)index_offset, idx, GL_STATIC_DRAW);
    BrScratchFree(idx);
    return buf;
}

br_geometry_stored *GeometryStoredGLAllocate(br_geometry_v1_model *gv1model, const char *id, br_renderer *r, struct v11model *model)
{
    size_t               total_vertices, total_faces;
    br_geometry_stored  *self;
    const GladGLContext *gl  = r->gl;
    br_gl_context_state *ctx = GLContextState(gl);

    self             = BrResAllocate(gv1model->renderer_facility->object_list, sizeof(*self), BR_MEMORY_OBJECT);
    self->dispatch   = &geometryStoredDispatch;
    self->identifier = BrResSprintf(self, BR_GLREND_DEBUG_USER_PREFIX "%s", id);
    self->device     = gv1model->device;
    self->gv1model   = gv1model;
    self->gl         = gl;

    ObjectContainerAddFront(gv1model->renderer_facility, (br_object *)self);

    self->model  = model;
    self->shared = BR_TRUE;

    self->groups = BrResAllocate(gv1model, sizeof(gl_groupinfo) * model->ngroups, BR_MEMORY_OBJECT_DATA);

    total_vertices = 0;
    total_faces    = 0;
    for(br_uint_16 i = 0; i < model->ngroups; ++i) {
        total_vertices += model->groups[i].nvertices;
        total_faces += model->groups[i].nfaces;
    }

    gl->BindVertexArray(0);

    self->gl_vbo = build_vbo(gl, model, total_vertices, self->groups);
    self->gl_ibo = build_ibo(gl, model, total_faces, self->groups);
    self->gl_vao = create_vao(gl, &ctx->main_shader, self->gl_vbo, self->gl_ibo);

    DeviceGLObjectLabelF(gl, GL_BUFFER, self->gl_vbo, "%s:vbo", self->identifier);
    DeviceGLObjectLabelF(gl, GL_BUFFER, self->gl_ibo, "%s:ibo", self->identifier);
    DeviceGLObjectLabelF(gl, GL_VERTEX_ARRAY, self->gl_vao, "%s:vao", self->identifier);

    gl->BindVertexArray(0);
    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    gl->BindBuffer(GL_ARRAY_BUFFER, 0);
    return (br_geometry_stored *)self;
}

static void BR_CMETHOD(br_geometry_stored_gl, free)(br_object *_self)
{
    br_geometry_stored  *self = (br_geometry_stored *)_self;
    const GladGLContext *gl   = self->gl;

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    /*
     * v246: Actor Studio/Create Part/copy-paste can leave a stored geometry
     * reachable only through the renderer-facility resource tree after the
     * renderer itself has gone away.  v245 frees every geometry still listed
     * in the facility while GL is alive, but a geometry no longer present in
     * that object list can survive until facility/device teardown.  Never call
     * through its cached GladGLContext unless the facility still owns a live
     * renderer using that exact dispatch table.
     */
    br_renderer *live_renderer = NULL;
    br_boolean gl_live = BR_FALSE;

    if(self->gv1model != NULL && self->gv1model->renderer_facility != NULL &&
       ObjectContainerFind((br_object_container *)self->gv1model->renderer_facility,
                           (br_object **)&live_renderer, BRT_RENDERER, NULL, NULL) == BRE_OK &&
       live_renderer != NULL && live_renderer->gl == gl) {
        gl_live = BR_TRUE;
    }

    ObjectContainerRemove(self->gv1model->renderer_facility, (br_object *)self);

    if(gl_live) {
        gl->DeleteVertexArrays(1, &self->gl_vao);
        gl->DeleteBuffers(1, &self->gl_vbo);
        gl->DeleteBuffers(1, &self->gl_ibo);
    } else {
        static br_uint_32 stale_free_diag;
        if(stale_free_diag < 64) {
            BrWarning("3DMM v246 geometry free skipped stale GL dispatch stored=%p gl=%p vao=%u vbo=%u ibo=%u",
                      (void *)self, (void *)gl, (unsigned)self->gl_vao,
                      (unsigned)self->gl_vbo, (unsigned)self->gl_ibo);
            ++stale_free_diag;
        }
    }
#else
    ObjectContainerRemove(self->gv1model->renderer_facility, (br_object *)self);

    gl->DeleteVertexArrays(1, &self->gl_vao);
    gl->DeleteBuffers(1, &self->gl_vbo);
    gl->DeleteBuffers(1, &self->gl_ibo);
#endif
    BrResFreeNoCallback(self);
}

static const char *BR_CMETHOD(br_geometry_stored_gl, identifier)(br_object *self)
{
    return ((br_geometry_stored *)self)->identifier;
}

static br_device *BR_CMETHOD(br_geometry_stored_gl, device)(br_object *self)
{
    return ((br_geometry_stored *)self)->device;
}

static br_token BR_CMETHOD(br_geometry_stored_gl, type)(br_object *self)
{
    return BRT_GEOMETRY_STORED;
}

static br_boolean BR_CMETHOD(br_geometry_stored_gl, isType)(br_object *self, br_token t)
{
    return (t == BRT_GEOMETRY_STORED) || (t == BRT_GEOMETRY) || (t == BRT_OBJECT);
}

static br_size_t BR_CMETHOD(br_geometry_stored_gl, space)(br_object *self)
{
    return sizeof(br_geometry_stored);
}

static br_tv_template *BR_CMETHOD(br_geometry_stored_gl, templateQuery)(br_object *_self)
{
    br_geometry_stored *self = (br_geometry_stored *)_self;

    if(self->device->templates.geometryStoredTemplate == NULL) {
        self->device->templates.geometryStoredTemplate = BrTVTemplateAllocate(self->device, templateEntries, BR_ASIZE(templateEntries));
    }

    return self->device->templates.geometryStoredTemplate;
}

static br_primitive *heapPrimitiveAdd(br_primitive_heap *heap, br_token type)
{
    br_primitive *p = (br_primitive *)heap->current;

    ASSERT_MESSAGE("Out of heap space", p != NULL);

    p->type = type;

    heap->current += sizeof(br_primitive);
    return p;
}

enum {
    RM_FORCE_BACK  = 0,
    RM_OPAQUE      = 1,
    RM_TRANS       = 2,
    RM_FORCE_FRONT = 3,
    RM_MAX         = 4,
};

static int get_render_mode(const state_stack *state)
{
    int         mode   = RM_OPAQUE;
    const char *reason = "opaque";

    if(state->valid & MASK_STATE_SURFACE) {
        if(state->surface.force_back) {
            mode   = RM_FORCE_BACK;
            reason = "force_back";
            goto done;
        }

        if(state->surface.force_front) {
            mode   = RM_FORCE_FRONT;
            reason = "force_front";
            goto done;
        }

        /* BRenderModern: Transparent? Defer. */
        if(state->surface.opacity < BR_SCALAR(1.0f)) {
            mode   = RM_TRANS;
            reason = "opacity";
            goto done;
        }
    }

    if(state->valid & MASK_STATE_PRIMITIVE) {
        /* BRenderModern: Blend flags set? Defer.*/
        if(state->prim.flags & PRIMF_BLEND) {
            mode   = RM_TRANS;
            reason = "blend_flag";
            goto done;
        }

        /* BRenderModern: Has a blend table? Defer. */
        if(state->prim.index_blend != NULL) {
            mode   = RM_TRANS;
            reason = "index_blend";
        }
    }

done:
#if BRENDER_LEGACY_3DMM_MODEL_ABI
    {
        static br_uint_32 diag_mode_count;
        if(diag_mode_count < 256) {
            BrWarning("3DMM glrend render-mode valid=0x%08X mode=%d reason=%s opacity_raw=0x%08X opacity=%g force_back=%d force_front=%d prim_flags=0x%08X blend=%d index_blend=%p",
                      (unsigned)state->valid, mode, reason, (unsigned)(br_uint_32)state->surface.opacity,
                      (double)BrScalarToFloat(state->surface.opacity), (int)state->surface.force_back,
                      (int)state->surface.force_front, (unsigned)state->prim.flags,
                      (state->prim.flags & PRIMF_BLEND) != 0, (void *)state->prim.index_blend);
            ++diag_mode_count;
        }
    }
#endif
    return mode;
}

/* BRenderModern:
 * Determine which bucket to dump things into. At least four are needed
 * for the various render types.
 *
 * | Type        | Ratio | DT | Order |
 * |-------------|-------|----|-------|
 * | Force back  |  5%   |    | BtF   |
 * | Opaque      | 80%   | X  | FtB   |
 * | Transparent | 10%   | X  | BtF   |
 * | Force front |  5%   |    | BtF   |
 *
 * DT  = Depth Tested
 * BtF = Back-to-front
 * TtB = Front-to-back
 */
static br_uint_16 calculate_bucket(const br_order_table *ot, const state_stack *state, br_scalar *depth)
{
    const br_scalar ratio_force_frontback = BR_SCALAR(0.05);
    const br_scalar ratio_transparent     = BR_SCALAR(0.10);
    br_uint_16      count_forced, count_opaque, count_trans;
    int             render_mode;
    br_scalar       ot_size;
    br_uint_16      base, count;
    br_scalar       tmp_depth;
    br_boolean      force_btf = (state->valid & MASK_STATE_OUTPUT) && state->output.depth == NULL;

    ASSERT(BR_ADD(BR_MUL(ratio_force_frontback, BR_SCALAR(2)), ratio_transparent) < BR_SCALAR(1.0));

    render_mode = get_render_mode(state);

    /* BRenderModern:
     * Case 1 - A single bucket.
     * Force back/front geometry to the back/front and pray
     * for the best. ¯\_(ツ)_/¯
     */
    if(ot->size == 1) {
        if(render_mode == RM_FORCE_BACK)
            *depth = BR_SCALAR_MAX;
        else if(render_mode == RM_FORCE_FRONT)
            *depth = BR_SCALAR(0.0);
        return 0;
    }

    /* BRenderModern:
     * Case 2 - 2 buckets.
     */
    if(ot->size == 2) {
        switch(render_mode) {
            case RM_FORCE_BACK:
                *depth = BR_SCALAR_MAX;
            case RM_OPAQUE:
            default:
                return 1;
            case RM_FORCE_FRONT:
                *depth = BR_SCALAR(0.0);
            case RM_TRANS:
                return 0;
        }
    }

    /* BRenderModern:
     * Case 3 - 3 buckets.
     */
    if(ot->size == 3) {
        switch(render_mode) {
            case RM_FORCE_BACK:
                *depth = BR_SCALAR_MAX;
            case RM_OPAQUE:
            default:
                return 2;

            case RM_TRANS:
                return 1;
            case RM_FORCE_FRONT:
                return 0;
        }
    }

    /* BRenderModern:
     * Case 4 - 4 buckets.
     */
    if(ot->size == 4) {
        switch(render_mode) {
            case RM_FORCE_BACK:
                return 3;
            case RM_OPAQUE:
            default:
                return 2;
            case RM_TRANS:
                return 1;
            case RM_FORCE_FRONT:
                return 0;
        }
    }

    /* BRenderModern:
     * Case 5 - >4 buckets
     */
#if BRENDER_LEGACY_3DMM_MODEL_ABI && BASED_FIXED
    /*
     * glrend was originally written for the modern floating-point scalar ABI.
     * In 3DMM compatibility builds br_scalar is 16.16 fixed, so multiplying
     * BR_SCALAR(size) by BR_SCALAR(ratio) with plain C multiplication
     * overflows instead of performing a fixed-point multiply.  Keep the
     * upstream partition semantics, but do the percentage calculation in
     * ordinary floating point because the result is an integer bucket count.
     */
    count_forced = (br_uint_16)ceilf((float)ot->size * 0.05f);
    count_trans  = (br_uint_16)((float)(ot->size - (count_forced << 1)) - ceilf((float)ot->size * 0.10f));
    count_opaque = ot->size - count_trans - (count_forced << 1);
#else
    ot_size      = BR_SCALAR(ot->size);
    count_forced = (br_uint_16)ceilf(ot_size * ratio_force_frontback);
    count_trans  = (br_uint_16)((float)(ot->size - (count_forced << 1)) - ceilf(ot_size * ratio_transparent));
    count_opaque = ot->size - count_trans - (count_forced << 1);
#endif

    ASSERT(count_forced + count_opaque + count_trans + count_forced == ot->size);

    tmp_depth = *depth;
    switch(render_mode) {
        case RM_FORCE_FRONT:
            base  = 0;
            count = count_forced;
            break;
        case RM_TRANS:
            base  = count_forced;
            count = count_opaque;
            break;
        case RM_OPAQUE:
        default:
            base  = count_forced + count_trans;
            count = count_opaque;

            if(!force_btf)
                tmp_depth = -tmp_depth;
            break;
        case RM_FORCE_BACK:
            base  = count_forced + count_trans + count_opaque;
            count = count_forced;
            break;
    }

#if BRENDER_LEGACY_3DMM_MODEL_ABI && BASED_FIXED
    {
        static br_uint_32 diag_count;
        if(diag_count < 128) {
            BrWarning("3DMM glrend bucket partition ot=%p size=%u mode=%d forced=%u trans_region=%u opaque_region=%u base=%u count=%u",
                      (void *)ot, (unsigned)ot->size, render_mode, (unsigned)count_forced, (unsigned)count_trans,
                      (unsigned)count_opaque, (unsigned)base, (unsigned)count);
            ++diag_count;
        }
    }

    if(count == 0) {
        BrWarning("3DMM glrend bucket partition produced zero-width region; using base bucket ot=%p size=%u mode=%d base=%u",
                  (void *)ot, (unsigned)ot->size, render_mode, (unsigned)base);
        return base < ot->size ? base : (br_uint_16)(ot->size - 1);
    }
#endif

    return base + BrZsPrimitiveBucketSelect(&tmp_depth, BR_PRIMITIVE_POINT, ot->min_z, ot->max_z, count, ot->type);
}

static br_boolean want_defer(const state_stack *state)
{
    const state_hidden *hidden = &state->hidden;

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    if(state->output.shadow_pass)
        return BR_FALSE;
#endif

    if(hidden->type != BRT_BUCKET_SORT)
        return BR_FALSE;

    if(hidden->divert == BRT_NONE)
        return BR_FALSE;

    if(hidden->divert == BRT_ALL)
        return BR_TRUE;

    UASSERT(hidden->divert == BRT_BLENDED);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    if(hidden->order_table != NULL && (hidden->order_table->size == 0 || hidden->order_table->table == NULL)) {
        BrWarning("3DMM glrend refusing invalid deferred order table ot=%p size=%u table=%p heap=%p",
                  (void *)hidden->order_table, (unsigned)hidden->order_table->size,
                  (void *)hidden->order_table->table, (void *)hidden->heap);
        return BR_FALSE;
    }
#endif

    return hidden->order_table != NULL && hidden->heap != NULL;
}

static br_error V1Model_RenderStored(br_geometry_stored *self, br_renderer *renderer, br_boolean type, br_boolean on_screen)
{
    br_primitive *prim;
    br_vector3    pos;
    br_boolean    defer;
    br_scalar     distance_from_zero;
    br_boolean    z_sorting;

    pos.v[0] = renderer->state.current->matrix.model_to_view.m[3][0];
    pos.v[1] = renderer->state.current->matrix.model_to_view.m[3][1];
    pos.v[2] = renderer->state.current->matrix.model_to_view.m[3][2];

    distance_from_zero = BrVector3Length(&pos);

    defer = want_defer(renderer->state.current);

    z_sorting = (renderer->state.current->valid & MASK_STATE_OUTPUT) && renderer->state.current->output.depth == NULL;

    for(int i = 0; i < self->model->ngroups; ++i) {
        struct v11group          *group     = self->model->groups + i;
        gl_groupinfo             *groupinfo = self->groups + i;
        br_renderer_state_stored *stored    = group->stored;
        int                       render_mode;
        br_uint_16                bucket;
        state_stack               state;

        /* BRenderModern:
         * If there's a stored state (i.e. a material), apply it to our current state.
         */
        renderer->state.current->render_type = type;
        state = *renderer->state.current;
        if(stored != NULL) {
            StateGLCopy(&state, &stored->state, MASK_STATE_STORED);
        }

        render_mode = get_render_mode(&state);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
        {
            static br_uint_32 diag_group_count;
            if(diag_group_count < 256) {
                BrWarning("3DMM glrend stored-group geom=%p group=%d verts=%u faces=%u stored=%p defer=%d z_sort=%d mode=%d hidden_type=%u divert=%u ot=%p heap=%p",
                          (void *)self, i, (unsigned)group->nvertices, (unsigned)group->nfaces, (void *)stored,
                          (int)defer, (int)z_sorting, render_mode, (unsigned)state.hidden.type,
                          (unsigned)state.hidden.divert, (void *)state.hidden.order_table, (void *)state.hidden.heap);
                ++diag_group_count;
            }
        }
#endif

        if(!defer) {
            RendererGLRenderGroup(renderer, self, groupinfo);
            continue;
        }

        bucket = calculate_bucket(state.hidden.order_table, &state, &distance_from_zero);

        if(z_sorting || render_mode == RM_TRANS) {
            /* BRenderModern:
             * If transparent or z-sorting, send things triangle-by-triangle.
             */
            state_stack *tmpstate = RendererGLAllocState(renderer, &state, group->nfaces);
            for(int f = 0; f < group->nfaces; ++f) {
                br_vector3 centroid, centroid_view;
                br_int_32  base;

                if((base = RendererGLNextImmTri(renderer, group, group->vertex_numbers[f])) < 0) {
                    /* BRenderModern:
                     * Out of immediate primitives.
                     */
                    BrLogWarn("GLREND", "Out of transparent primitives.");
                    RendererGLUnrefState(renderer, tmpstate);
                    continue;
                }

                prim         = heapPrimitiveAdd(state.hidden.heap, BRT_TRIANGLE);
                prim->stored = stored;
                prim->v[0]   = (void *)(br_uintptr_t)base;
                prim->v[1]   = tmpstate;
                prim->v[2]   = groupinfo;

                centroid = DeviceGLTriangleCentroid(&renderer->trans.pool[base + 0].position, &renderer->trans.pool[base + 1].position,
                                                    &renderer->trans.pool[base + 2].position);
                BrMatrix34ApplyP(&centroid_view, &centroid, &state.matrix.model_to_view);

                prim->depth = -BrVector3Length(&centroid_view);

                /* BRenderModern:
                 * If the user set a function defer to them.
                 */
                if(state.hidden.insert_fn != NULL) {
                    state.hidden.insert_fn(prim, state.hidden.insert_arg1, state.hidden.insert_arg2, state.hidden.insert_arg3,
                                           state.hidden.order_table, &prim->depth);
                    continue;
                }

                BrZsOrderTablePrimitiveInsert(state.hidden.order_table, prim, bucket);
            }
        } else {
            /* BRenderModern:
             * Render everything else by group.
             */
            prim         = heapPrimitiveAdd(state.hidden.heap, BRT_GEOMETRY_STORED);
            prim->stored = stored;
            prim->v[0]   = self;
            prim->v[1]   = RendererGLAllocState(renderer, &state, 1);
            prim->v[2]   = groupinfo;
            prim->depth  = distance_from_zero;

            /* BRenderModern:
             * If the user set a function defer to them.
             */
            if(state.hidden.insert_fn != NULL) {
                state.hidden.insert_fn(prim, state.hidden.insert_arg1, state.hidden.insert_arg2, state.hidden.insert_arg3,
                                       state.hidden.order_table, &prim->depth);
                continue;
            }

            BrZsOrderTablePrimitiveInsert(state.hidden.order_table, prim, bucket);
        }
    }
    return BRE_OK;
}

static br_error BR_CMETHOD(br_geometry_stored_gl, render)(br_geometry_stored *self, br_renderer *renderer, br_token type)
{
    if(type != BRT_TRIANGLE && type != BRT_POINT && type != BRT_LINE)
        return BRE_FAIL;

    return V1Model_RenderStored(self, renderer, type, BR_FALSE);
}

static br_error BR_CMETHOD(br_geometry_stored_gl, renderOnScreen)(br_geometry_stored *self, br_renderer *renderer, br_token type)
{
    if(type != BRT_TRIANGLE && type != BRT_POINT && type != BRT_LINE)
        return BRE_FAIL;

    return V1Model_RenderStored(self, renderer, type, BR_TRUE);
}

static const struct br_geometry_stored_dispatch geometryStoredDispatch = {
    .__reserved0 = NULL,
    .__reserved1 = NULL,
    .__reserved2 = NULL,
    .__reserved3 = NULL,
    ._free       = BR_CMETHOD(br_geometry_stored_gl, free),
    ._identifier = BR_CMETHOD(br_geometry_stored_gl, identifier),
    ._type       = BR_CMETHOD(br_geometry_stored_gl, type),
    ._isType     = BR_CMETHOD(br_geometry_stored_gl, isType),
    ._device     = BR_CMETHOD(br_geometry_stored_gl, device),
    ._space      = BR_CMETHOD(br_geometry_stored_gl, space),

    ._templateQuery = BR_CMETHOD(br_geometry_stored_gl, templateQuery),
    ._query         = BR_CMETHOD(br_object, query),
    ._queryBuffer   = BR_CMETHOD(br_object, queryBuffer),
    ._queryMany     = BR_CMETHOD(br_object, queryMany),
    ._queryManySize = BR_CMETHOD(br_object, queryManySize),
    ._queryAll      = BR_CMETHOD(br_object, queryAll),
    ._queryAllSize  = BR_CMETHOD(br_object, queryAllSize),

    ._render         = BR_CMETHOD(br_geometry_stored_gl, render),
    ._renderOnScreen = BR_CMETHOD(br_geometry_stored_gl, renderOnScreen),
};
