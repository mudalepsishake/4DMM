/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: render.c 1.9 1998/11/12 13:16:45 johng Exp $
 * $Locker: $
 *
 * Traversal of hierachy for rendering
 */

#include "v1db.h"
#include "shortcut.h"
#include "brassert.h"

#include "math_ip.h"

#if BRENDER_BUILD_FOR_CROC
#include <csource/strat.h>
#define MarkStratAsDrawn(a) \
    if((a) > (void *)1024)  \
        ((STRAT *)(a))->flag_2 |= ST_2_ONSCREEN;
#define MarkStratAsNotDrawn(a) \
    if((a) > (void *)1024)     \
        ((STRAT *)(a))->flag_2 &= ~ST_2_ONSCREEN;
#else
#define MarkStratAsDrawn(a)
#define MarkStratAsNotDrawn(a)
#endif

static void actorRenderOnScreen(br_actor *ap, br_model *model, br_material *material, void *render_data, br_uint_8 style, br_uint_16 t);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
static br_token Br4DMMShadowPassToken(void)
{
    static br_token token = BR_NULL_TOKEN;
    if(token == BR_NULL_TOKEN)
        token = BrTokenCreate("SHADOW_PASS_B", BRT_BOOLEAN);
    return token;
}

static br_token Br4DMMShadowBlockerPassToken(void)
{
    static br_token token = BR_NULL_TOKEN;
    if(token == BR_NULL_TOKEN)
        token = BrTokenCreate("SHADOW_BLOCKER_PASS_B", BRT_BOOLEAN);
    return token;
}

static br_token Br4DMMShadowDetailPassToken(void)
{
    static br_token token = BR_NULL_TOKEN;
    if(token == BR_NULL_TOKEN)
        token = BrTokenCreate("SHADOW_DETAIL_PASS_B", BRT_BOOLEAN);
    return token;
}

static br_token Br4DMMShadowOwnerToken(void)
{
    static br_token token = BR_NULL_TOKEN;
    if(token == BR_NULL_TOKEN)
        token = BrTokenCreate("SHADOW_OWNER_U32", BRT_UINT_32);
    return token;
}

static br_token Br4DMMShadowModelToLightValidToken(void)
{
    static br_token token = BR_NULL_TOKEN;
    if(token == BR_NULL_TOKEN)
        token = BrTokenCreate("SHADOW_MODEL_TO_LIGHT_VALID_B", BRT_BOOLEAN);
    return token;
}

static br_token Br4DMMShadowModelToLightToken(void)
{
    static br_token token = BR_NULL_TOKEN;
    if(token == BR_NULL_TOKEN) {
#if BASED_FIXED
        token = BrTokenCreate("SHADOW_MODEL_TO_LIGHT_M34_X", BRT_MATRIX34_FIXED);
#else
        token = BrTokenCreate("SHADOW_MODEL_TO_LIGHT_M34_F", BRT_MATRIX34_FLOAT);
#endif
    }
    return token;
}

/*
 * v227: the owner attachment is now GL_R8UI so a 16K owner map costs 256 MiB.
 * Slots 1..254 retain exact logical-object identity and slot 0 remains "empty".
 * Slot 255 is reserved as an overflow marker. v269 permits same-owner
 * shadows once caster/receiver light-space depth separation exceeds the
 * same-surface guard; every non-zero slot remains a valid caster marker,
 * including overflow objects, while retaining exact identity for diagnostics.
 */
#define BR_4DMM_SHADOW_OWNER_SLOT_EXACT_MAX 254u
#define BR_4DMM_SHADOW_OWNER_SLOT_OVERFLOW 255u
static const void *v4dmm_shadow_owner_keys[BR_4DMM_SHADOW_OWNER_SLOT_EXACT_MAX];
static br_uint_32 v4dmm_shadow_owner_key_count = 0;
static br_boolean v4dmm_shadow_owner_slot_overflow_logged = BR_FALSE;

static void Br4DMMShadowOwnerSlotsReset(void)
{
    v4dmm_shadow_owner_key_count = 0;
    v4dmm_shadow_owner_slot_overflow_logged = BR_FALSE;
}

static br_uint_32 Br4DMMShadowOwnerId(const br_actor *actor)
{
    const void *key;
    br_uint_32 i;

    if(actor == NULL)
        return 0;

    /* BODY parts share their owning BODY identifier; other models use actor. */
    key = actor->identifier != NULL ? (const void *)actor->identifier : (const void *)actor;

    for(i = 0; i < v4dmm_shadow_owner_key_count; ++i) {
        if(v4dmm_shadow_owner_keys[i] == key)
            return i + 1u;
    }

    if(v4dmm_shadow_owner_key_count < BR_4DMM_SHADOW_OWNER_SLOT_EXACT_MAX) {
        v4dmm_shadow_owner_keys[v4dmm_shadow_owner_key_count] = key;
        ++v4dmm_shadow_owner_key_count;
        return v4dmm_shadow_owner_key_count;
    }

    if(!v4dmm_shadow_owner_slot_overflow_logged) {
        BrWarning("SHADOW owner_slots_v269 overflow exact_max=%u; slot=%u remains valid caster marker",
                  (unsigned)BR_4DMM_SHADOW_OWNER_SLOT_EXACT_MAX,
                  (unsigned)BR_4DMM_SHADOW_OWNER_SLOT_OVERFLOW);
        v4dmm_shadow_owner_slot_overflow_logged = BR_TRUE;
    }

    return BR_4DMM_SHADOW_OWNER_SLOT_OVERFLOW;
}

/* BODY-root policy bits supplied by 4DMM through BR_ACTOR_NONE::type_data. */
#define BR_4DMM_SHADOW_NO_CAST_SYSTEM 0x00000001u
#define BR_4DMM_SHADOW_NO_CAST_OBJECT 0x00000002u
#define BR_4DMM_SHADOW_FLUSH_OVERLAP  0x00000004u
#define BR_4DMM_SHADOW_PACK_FLAG_FLUSH 0x00000100u

static br_uint_32 Br4DMMShadowBodyFlags(const br_actor *actor)
{
    const br_actor *root = actor;

    while(root != NULL) {
        if(root->type == BR_ACTOR_NONE && root->identifier != NULL && root->type_data != NULL) {
            /* Compatibility with the pre-v232 Light Object marker. */
            if(root->type_data == (void *)root->identifier)
                return BR_4DMM_SHADOW_NO_CAST_SYSTEM;
            return *(const br_uint_32 *)root->type_data;
        }
        root = root->parent;
    }

    return 0;
}

static br_uint_32 Br4DMMShadowPackedOwner(const br_actor *actor)
{
    br_uint_32 packed = Br4DMMShadowOwnerId(actor) & 0xFFu;
    if((Br4DMMShadowBodyFlags(actor) & BR_4DMM_SHADOW_FLUSH_OVERLAP) != 0)
        packed |= BR_4DMM_SHADOW_PACK_FLAG_FLUSH;
    return packed;
}

/*
 * Keep shadow traversal explicitly identifiable so the diagnostic pass can
 * apply temporary caster policy without changing ordinary colour rendering.
 * v219 shadow casters no longer obey main-camera visibility. Selection-edge
 * actors are still never submitted.
 */
static br_boolean v4dmm_shadow_traversal = BR_FALSE;
static br_boolean v4dmm_shadow_blocker_traversal = BR_FALSE;
static br_uint_32 v4dmm_shadow_models = 0;
static br_uint_32 v4dmm_shadow_blocker_models = 0;
static br_uint_32 v4dmm_shadow_detail_models = 0;
static br_boolean v4dmm_shadow_detail_traversal = BR_FALSE;
static br_uint_32 v4dmm_shadow_skipped_edges = 0;
static br_uint_32 v4dmm_shadow_skipped_light_objects = 0;
static br_uint_32 v4dmm_shadow_skipped_null_models = 0;
static br_uint_32 v4dmm_shadow_null_model_diag = 0;
static br_boolean v4dmm_shadow_trace_enabled = BR_FALSE;
static br_uint_32 v4dmm_shadow_trace_frame = 0;
static br_uint_32 v4dmm_shadow_trace_draw = 0;

static br_actor *Br4DMMShadowLightActor(void);

/*
 * v219 removes the last main-camera dependency from caster selection.
 * v217 fixed BODY-part amputation by promoting visibility to the logical-owner
 * level, but whole owners still entered/left the shadow map whenever the Free
 * Cam crossed their screen bounds.  After v218 made projection light-local,
 * that owner-population change is now the remaining camera-controlled input.
 * Render every valid world caster and let the spotlight clip volume discard
 * geometry that cannot contribute.
 */
void BR_PUBLIC_ENTRY Br4DMMShadowTraceSetEnabled(br_boolean enabled)
{
    v4dmm_shadow_trace_enabled = enabled ? BR_TRUE : BR_FALSE;
}

/*
 * BODY-root shadow policy is inherited by all parts of one logical 3DMM
 * object.  System exclusions (Light Object authoring handles) and the new
 * user-facing "shadow casting 0" property both suppress only the shadow
 * traversal; colour rendering remains unchanged.
 */
static br_boolean Br4DMMShadowExcludedRoot(const br_actor *actor)
{
    br_uint_32 flags;

    if(actor == NULL || actor->type != BR_ACTOR_NONE)
        return BR_FALSE;

    flags = Br4DMMShadowBodyFlags(actor);

    /* Light-object authoring handles never participate in either shadow layer. */
    if((flags & BR_4DMM_SHADOW_NO_CAST_SYSTEM) != 0)
        return BR_TRUE;

    /*
     * v233 splits "cast a shadow" from "stop an incoming shadow".
     * The ordinary shadow pass prunes user no-cast objects.  The blocker pass
     * cannot prune generic BR_ACTOR_NONE nodes because Object Groups and other
     * structural parents may contain a no-cast BODY farther down the tree.
     * Per-model policy below performs the exact blocker selection.
     */
    if(!v4dmm_shadow_blocker_traversal && (flags & BR_4DMM_SHADOW_NO_CAST_OBJECT) != 0)
        return BR_TRUE;

    return BR_FALSE;
}

static br_boolean Br4DMMShadowModelPolicySkip(const br_actor *actor)
{
    br_uint_32 flags;

    if(!v4dmm_shadow_traversal || actor == NULL)
        return BR_FALSE;

    flags = Br4DMMShadowBodyFlags(actor);
    if((flags & BR_4DMM_SHADOW_NO_CAST_SYSTEM) != 0)
        return BR_TRUE;

    if(v4dmm_shadow_blocker_traversal)
        return (flags & BR_4DMM_SHADOW_NO_CAST_OBJECT) == 0;

    return (flags & BR_4DMM_SHADOW_NO_CAST_OBJECT) != 0;
}

static void Br4DMMShadowModelRendered(void)
{
    if(!v4dmm_shadow_traversal)
        return;

    if(v4dmm_shadow_blocker_traversal)
        ++v4dmm_shadow_blocker_models;
    else if(v4dmm_shadow_detail_traversal)
        ++v4dmm_shadow_detail_models;
    else
        ++v4dmm_shadow_models;
}

/*
 * 3DMM BODY nodes may temporarily retain their hierarchy while carrying no
 * model. Ordinary BRender inheritance would render v1db.default_model for a
 * BR_ACTOR_MODEL with model == NULL, producing the transient cube impostors
 * seen in the shadow map. During shadow traversal only, suppress that actor's
 * own draw while still traversing its children.
 */
static br_boolean Br4DMMShadowNullModel(const br_actor *actor)
{
    if(!v4dmm_shadow_traversal || actor == NULL || actor->type != BR_ACTOR_MODEL || actor->model != NULL)
        return BR_FALSE;

    ++v4dmm_shadow_skipped_null_models;
    if(v4dmm_shadow_null_model_diag < 24) {
        BrWarning("SHADOW skip_null_model n=%u actor=%p identifier=%p style=%u parent=%p",
                  (unsigned)v4dmm_shadow_null_model_diag, actor, actor->identifier,
                  (unsigned)actor->render_style, actor->parent);
        ++v4dmm_shadow_null_model_diag;
    }
    return BR_TRUE;
}

/*
 * Record the exact model and composed transform submitted to the shadow map.
 * This is intentionally opt-in because animated BODY hierarchies can generate
 * thousands of lines per second.  It is used to catch stale model/transform
 * state that cannot be seen in BWLD's top-level world-child diagnostics.
 */
static void Br4DMMShadowTraceCaster(const br_actor *actor, const br_model *model, br_uint_8 style, br_token on_screen)
{
    br_matrix34 m2v;
    br_size_t dummy = 0;

    if(!v4dmm_shadow_trace_enabled || !v4dmm_shadow_traversal || actor == NULL || model == NULL)
        return;

    RendererPartQueryBuffer(v1db.renderer, BRT_MATRIX, 0, &dummy, (br_uint_32 *)&m2v, sizeof(m2v),
                            BRT_AS_MATRIX34_SCALAR(MODEL_TO_VIEW));

    br_matrix34 direct_m2l;
    br_actor *shadow_light_actor = Br4DMMShadowLightActor();
    if(shadow_light_actor != NULL)
        BrActorToActorMatrix34(&direct_m2l, actor, shadow_light_actor);
    else
        BrMatrix34Identity(&direct_m2l);

    BrWarning("SHADOW TRACE caster frame=%u draw=%u owner=0x%08X onscreen=%u actor=%p parent=%p actor_model=%p resolved_model=%p is_default=%u verts=%u faces=%u style=%u "
              "m2v_t=(%.7g,%.7g,%.7g) direct_m2l_t=(%.7g,%.7g,%.7g) bounds_min=(%.7g,%.7g,%.7g) bounds_max=(%.7g,%.7g,%.7g) direct_actor_to_light_v220=1",
              (unsigned)v4dmm_shadow_trace_frame, (unsigned)v4dmm_shadow_trace_draw++,
              (unsigned)Br4DMMShadowOwnerId(actor), (unsigned)on_screen,
              actor, actor->parent, actor->model, model, (unsigned)(model == v1db.default_model),
              (unsigned)model->nvertices, (unsigned)model->nfaces, (unsigned)style,
              (double)BrScalarToFloat(m2v.m[3][0]), (double)BrScalarToFloat(m2v.m[3][1]),
              (double)BrScalarToFloat(m2v.m[3][2]),
              (double)BrScalarToFloat(direct_m2l.m[3][0]), (double)BrScalarToFloat(direct_m2l.m[3][1]),
              (double)BrScalarToFloat(direct_m2l.m[3][2]),
              (double)BrScalarToFloat(model->bounds.min.v[0]), (double)BrScalarToFloat(model->bounds.min.v[1]),
              (double)BrScalarToFloat(model->bounds.min.v[2]), (double)BrScalarToFloat(model->bounds.max.v[0]),
              (double)BrScalarToFloat(model->bounds.max.v[1]), (double)BrScalarToFloat(model->bounds.max.v[2]));
}

static br_actor *Br4DMMShadowLightActor(void)
{
    if(v1db.enabled_lights.enabled == NULL)
        return NULL;

    for(int i = 0; i < v1db.enabled_lights.max; ++i) {
        br_actor *actor = v1db.enabled_lights.enabled[i];
        if(actor == NULL || actor->type_data == NULL)
            continue;

        const br_light *light = actor->type_data;
        if((light->type & BR_LIGHT_SHADOW) != 0 && (light->type & BR_LIGHT_TYPE) == BR_LIGHT_SPOT)
            return actor;
    }

    return NULL;
}

static br_boolean Br4DMMShadowLightEnabled(void)
{
    return Br4DMMShadowLightActor() != NULL;
}
#endif

void BR_PUBLIC_ENTRY BrDbModelRender(br_actor *actor, br_model *model, br_material *material, void *render_data, br_uint_8 style,
                                     int on_screen, int use_custom)
{
    br_int_32      count;
    br_token_value tv[] = {
        {BRT_V1INSERT_FUNCTION_P, 0},
        {BRT_V1INSERT_ARG1_P,     0},
        {BRT_V1INSERT_ARG2_P,     0},
        {BRT_V1INSERT_ARG3_P,     0},
        {0,                       0},
    };

    UASSERT(v1db.rendering != 0);
    UASSERT(model != NULL);
    UASSERT(material != NULL);
    UASSERT(actor != NULL);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    /*
     * Tag every submitted model with its logical 3DMM object owner. The shadow
     * depth pass writes the same owner into a parallel integer attachment. v269
     * uses zero/non-zero ownership to reject empty/stale base depth and exact
     * ownership to apply only the narrow same-surface depth guard, while still
     * allowing legitimate occlusion between separated surfaces of one object.
     */
    RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, Br4DMMShadowOwnerToken(),
                    (br_value){.u32 = v4dmm_shadow_blocker_traversal ? 0u : Br4DMMShadowPackedOwner(actor)});

    /*
     * v220: derive model->light directly from the actor hierarchy.  v218
     * multiplied model->view by view->light, which algebraically cancels the
     * camera but still lets fixed/scalar quantisation from two camera-relative
     * matrices leak into the final shadow transform while Free Cam moves.
     * BrActorToActorMatrix34() never passes through the camera at all.
     */
    br_actor *shadow_light_actor = Br4DMMShadowLightActor();
    if(shadow_light_actor != NULL) {
        br_matrix34 model_to_light;
        BrActorToActorMatrix34(&model_to_light, actor, shadow_light_actor);
        RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, Br4DMMShadowModelToLightToken(),
                        (br_value){.m34 = &model_to_light});
        RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, Br4DMMShadowModelToLightValidToken(),
                        (br_value){.b = BR_TRUE});
    } else {
        RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, Br4DMMShadowModelToLightValidToken(),
                        (br_value){.b = BR_FALSE});
    }
#endif

    /* BRender:
     * Mark local copy of model_to_screen as invalid
     */
    v1db.model_to_screen_valid = BR_FALSE;

    /* BRender:
     * If model has custom callback, keep following it until
     * a model is reached
     */
    if(use_custom && (model->flags & BR_MODF_CUSTOM)) {
        /* BRender:
         * XXX Fetch current transforms from renderer
         */
        model->custom(actor, model, material, render_data, style, on_screen);
        return;
    }

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    /*
     * 3DMM legitimately uses empty model actors as structural placeholders.
     * They carry no renderable geometry, but their actor children may contain
     * the real prepared models. Modern BRender normally treats an unprepared
     * model as fatal, which aborts traversal before those children are reached.
     * Preserve the strict failure for every non-empty model and only make the
     * historical 0-vertex/0-face placeholder a no-op render.
     */
    if(model->prepared == NULL && model->stored == NULL && model->nvertices == 0 && model->nfaces == 0)
        return;
#endif

    if(model->prepared == NULL && model->stored == NULL)
        BR_ERROR1("Tried to render un-prepared model %s", model->identifier ? model->identifier : "<NULL>");

    /* BRenderModern: Set the surface, primitive, and culling states for batched rendering (OpenGL 2.0+) */
    if(material == NULL || material->stored == NULL)
        RendererStateDefault(v1db.renderer, (br_uint_32)(BR_STATE_PRIMITIVE | BR_STATE_SURFACE | BR_STATE_CULL));
    else
        RendererStateRestore(v1db.renderer, material->stored, (br_uint_32)(BR_STATE_PRIMITIVE | BR_STATE_SURFACE | BR_STATE_CULL));

    /* BRender:
     * Optional preparation for Z-Sort
     */
    if(render_data != NULL) {
        br_order_table *ot = render_data;

        SetOrderTableBounds(&model->bounds, ot);

        if(ot->visits == 0) {
            BrZsOrderTableClear(ot);
            InsertOrderTableList(ot);
        }

        ot->visits++;

        RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_V1ORDER_TABLE_P, (br_value){.p = ot});

        /* BRender:
         * See if a 'primitive insertion' function needs to be added
         */
        if(v1db.primitive_call) {

            tv[0].v.p = v1db.primitive_call;
            tv[1].v.p = actor;
            tv[2].v.p = model;
            tv[3].v.p = material;

            RendererPartSetMany(v1db.renderer, BRT_HIDDEN_SURFACE, 0, tv, &count);
        }
    }

    if(v1db.bounds_call) {
        br_int_32      c;
        char           buffer[sizeof(br_vector2) * 2];
        br_token_value tv[] = {
            {BRT_AS_VECTOR2_SCALAR(MIN), 0},
            {BRT_AS_VECTOR2_SCALAR(MAX), 0},
            {0,                          0},
        };

        br_int_32 int_bounds[4];

        /* BRender:
         * Clear bounds
         */
        RendererStateDefault(v1db.renderer, BR_STATE_BOUNDS);

        RenderStyleCalls[style](actor, model, material, render_data, style, on_screen);

        /* BRender:
         * Fetch bounds and call user op.
         */
        RendererPartQueryMany(v1db.renderer, BRT_BOUNDS, 0, tv, buffer, sizeof(buffer), &c);

        int_bounds[0] = BrScalarToInt(((br_vector3 *)tv[0].v.p)->v[0]);
        int_bounds[1] = BrScalarToInt(((br_vector3 *)tv[0].v.p)->v[1]);
        int_bounds[2] = BrScalarToInt(((br_vector3 *)tv[1].v.p)->v[0]);
        int_bounds[3] = BrScalarToInt(((br_vector3 *)tv[1].v.p)->v[1]);

        /* BRender:
         * Clamp to screen boundary
         */
        if(int_bounds[0] < 0)
            int_bounds[0] = 0;
        if(int_bounds[1] < 0)
            int_bounds[1] = 0;
        if(int_bounds[2] < 0)
            int_bounds[2] = 0;
        if(int_bounds[3] < 0)
            int_bounds[3] = 0;

        if(int_bounds[0] >= v1db.colour_buffer->width)
            int_bounds[0] = v1db.colour_buffer->width - 1;

        if(int_bounds[1] >= v1db.colour_buffer->height)
            int_bounds[1] = v1db.colour_buffer->height - 1;

        if(int_bounds[2] >= v1db.colour_buffer->width)
            int_bounds[2] = v1db.colour_buffer->width - 1;

        if(int_bounds[3] >= v1db.colour_buffer->height)
            int_bounds[3] = v1db.colour_buffer->height - 1;

        /* BRender:
         * Make relative to screen origon
         */
        int_bounds[0] -= v1db.colour_buffer->origin_x;
        int_bounds[1] -= v1db.colour_buffer->origin_y;
        int_bounds[2] -= v1db.colour_buffer->origin_x;
        int_bounds[3] -= v1db.colour_buffer->origin_y;

        if((int_bounds[0] <= int_bounds[2]) && (int_bounds[1] <= int_bounds[3])) {
            /* BRender:
             * XXX Must make sure model_to_screen is up to date
             */
            v1db.bounds_call(actor, model, material, render_data, style, &v1db.model_to_screen, int_bounds);
        }

    } else {
        RenderStyleCalls[style](actor, model, material, render_data, style, on_screen);
    }
    MarkStratAsDrawn(actor->user);
}

br_uint_32 BR_PUBLIC_ENTRY BrOnScreenCheck(br_bounds3 *bounds)
{
    br_token r;
    UASSERT_MESSAGE("Invalid BrOnScreenCheck pointer", bounds != NULL);
    RendererBoundsTest(v1db.renderer, &r, (void *)bounds);

    return r;
}

/* BRender:
 * Prepend an actor's transform onto the model->view transform and return the new combined transform
 * type
 */
static br_uint_16 prependActorTransform(br_actor *ap, br_uint_16 t)
{
    br_matrix34 mt;
    ASSERT_MESSAGE("Invalid prependActorTransform pointer", ap != NULL);

#if 0
	/* BRender:
	 * See if this actor is on the camera path - if so, generate a new transform
	 */
	if(ap == v1db.camera_path[ap->depth].a) {

		RendererPartSet(v1db.renderer, BRT_MATRIX, 0,
						BRT_AS_MATRIX34_SCALAR(MODEL_TO_VIEW), (br_value){.m34 = &v1db.camera_path[ap->depth].m});
		t = v1db.camera_path[ap->depth].transform_type;

		RendererPartSet(v1db.renderer, BRT_MATRIX, 0,
						BRT_MODEL_TO_VIEW_HINT_T, (br_value){.t = BrTransformTypeIsLP(t) ? BRT_LENGTH_PRESERVING : BRT_NONE)});

		RendererModelInvert(v1db.renderer);
	} else {
		if(BrTransformTypeIsMatrix34(ap->t.type))
			RendererModelMul(v1db.renderer, (void *)&ap->t.t.mat);
		else {
			BrTransformToMatrix34(&mt, &ap->t);
			RendererModelMul(v1db.renderer, (void *)&mt);
		}
		t = BrTransformCombineTypes(t, ap->t.type);

		RendererPartSet(v1db.renderer, BRT_MATRIX, 0,
						BRT_MODEL_TO_VIEW_HINT_T, (br_value){.t = BrTransformTypeIsLP(t) ? BRT_LENGTH_PRESERVING : BRT_NONE});
	}
#else
    if(BrTransformTypeIsMatrix34(ap->t.type)) {
        RendererModelMul(v1db.renderer, (void *)&ap->t.t.mat);
    } else {
        BrTransformToMatrix34(&mt, &ap->t);
        RendererModelMul(v1db.renderer, (void *)&mt);
    }
    t = BrTransformCombineTypes(t, ap->t.type);

    RendererPartSet(v1db.renderer, BRT_MATRIX, 0, BRT_MODEL_TO_VIEW_HINT_T,
                    (br_value){.t = BrTransformTypeIsLP(t) ? BRT_LENGTH_PRESERVING : BRT_NONE});
#endif

    return t;
}

static br_uint_16 prependMatrix(br_matrix34 *mat, br_uint_16 mat_t, br_uint_16 t)
{
    ASSERT_MESSAGE("Invalid prependMatrix pointer", mat != NULL);

    RendererModelMul(v1db.renderer, (void *)mat);

    t = BrTransformCombineTypes(t, mat_t);

    RendererPartSet(v1db.renderer, BRT_MATRIX, 0, BRT_MODEL_TO_VIEW_HINT_T,
                    (br_value){.t = BrTransformTypeIsLP(t) ? BRT_LENGTH_PRESERVING : BRT_NONE});

    return t;
}

/* BRender:
 * Rendering traversal for the given actor
 */
static void actorRender(br_actor *ap, br_model *model, br_material *material, void *render_data, br_uint_8 style, br_uint_16 t)
{
    /* BRender:
     * Saved state
     */
    br_material *this_material;
    br_model    *this_model;
    void        *this_render_data;
    br_actor    *a;
    br_token     s;

    ASSERT_MESSAGE("Invalid actorRender pointer", model != NULL);
    ASSERT_MESSAGE("Invalid actorRender pointer", material != NULL);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    if(v4dmm_shadow_traversal && Br4DMMShadowExcludedRoot(ap)) {
        ++v4dmm_shadow_skipped_light_objects;
        return;
    }
#endif

    MarkStratAsNotDrawn(ap->user);

    /* BRender:
     * See if this actor overrides default material, model, render_data or style
     */
    if(ap->render_style != BR_RSTYLE_DEFAULT)
        style = ap->render_style;

    if(style == BR_RSTYLE_NONE)
        return;

    /* BRender:
     * Ignore actors with no children that are not models, and actors with renderstyle = NONE
     */
    if(ap->children == NULL && ap->type != BR_ACTOR_MODEL)
        return;

    this_material    = ap->material ? ap->material : material;
    this_model       = ap->model ? ap->model : model;
    this_render_data = ap->render_data ? ap->render_data : render_data;

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    if(v4dmm_shadow_traversal)
        this_render_data = NULL;
#endif

    /* BRender:
     * Catch special case of identity transforms
     */
    if(ap->t.type == BR_TRANSFORM_IDENTITY) {
        /** BRender:
         ** Actor has no transform
         **/
        switch(ap->type) {

            case BR_ACTOR_MODEL:
                /*
                 * Shadow traversal deliberately ignores the main camera.
                 * The spotlight projection clips non-contributing geometry.
                 */
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                if(v4dmm_shadow_traversal)
                    s = OSC_ACCEPT;
                else
#endif
                    s = BrOnScreenCheck(&this_model->bounds);

                if(s != OSC_REJECT) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                    if(Br4DMMShadowModelPolicySkip(ap)) {
                        /* Keep walking children; this model belongs to the other shadow layer. */
                    } else if(v4dmm_shadow_traversal && style == BR_RSTYLE_BOUNDING_EDGES) {
                        ++v4dmm_shadow_skipped_edges;
                    } else if(Br4DMMShadowNullModel(ap)) {
                        /* Keep traversing children below. */
                    } else
#endif
                    {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                        Br4DMMShadowTraceCaster(ap, this_model, style, s);
#endif
                        BrLightCullReset();
                        BrDbModelRender(ap, this_model, this_material, this_render_data, style, s, 1);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                        Br4DMMShadowModelRendered();
#endif
                    }
                }
                break;

            case BR_ACTOR_BOUNDS:
                /*
                 * Camera hierarchy bounds do not control shadow casters.
                 */
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                if(v4dmm_shadow_traversal)
                    break;
#endif
                if(BrOnScreenCheck(ap->type_data) == OSC_REJECT)
                    /* BRender: DONT PROCESS CHILDREN */
                    return;
                break;

            case BR_ACTOR_BOUNDS_CORRECT:
                /* BRender:
                 * A garuanteed bounding box - test to see if it is on screen
                 */
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                if(v4dmm_shadow_traversal)
                    break;
#endif
                switch(BrOnScreenCheck(ap->type_data)) {

                    case OSC_ACCEPT:
                        /* BRender:
                         * Bounding box is completely on screen - process children with special loop
                         */
                        BR_FOR_SIMPLELIST(&ap->children, a)
                            actorRenderOnScreen(a, this_model, this_material, this_render_data, style, t);
                        /* BRender: FALL THROUGH */

                    case OSC_REJECT:
                        /* BRender: DONT PROCESS CHILDREN */
                        return;
                }
        }

        /* BRender:
         * Recurse for children
         */
        BR_FOR_SIMPLELIST(&ap->children, a)
            actorRender(a, this_model, this_material, this_render_data, style, t);

        return;
    }

    /** BRender:
     ** Actor has a transform
     **/

    /* BRender:
     * Save the current transforms
     */
    RendererStatePush(v1db.renderer, BR_STATE_MATRIX);

    t = prependActorTransform(ap, t);

    switch(ap->type) {

        case BR_ACTOR_MODEL:
#if BRENDER_LEGACY_3DMM_MODEL_ABI
            if(v4dmm_shadow_traversal)
                s = OSC_ACCEPT;
            else
#endif
                s = BrOnScreenCheck(&this_model->bounds);

            if(s != OSC_REJECT) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                if(Br4DMMShadowModelPolicySkip(ap)) {
                    /* Keep walking children; this model belongs to the other shadow layer. */
                } else if(v4dmm_shadow_traversal && style == BR_RSTYLE_BOUNDING_EDGES) {
                    ++v4dmm_shadow_skipped_edges;
                } else if(Br4DMMShadowNullModel(ap)) {
                    /* Keep traversing children below. */
                } else
#endif
                {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                    Br4DMMShadowTraceCaster(ap, this_model, style, s);
#endif
                    BrLightCullReset();
                    BrDbModelRender(ap, this_model, this_material, this_render_data, style, s, 1);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                    Br4DMMShadowModelRendered();
#endif
                }
            }
            break;

        case BR_ACTOR_BOUNDS:
            /*
             * Camera hierarchy bounds do not control shadow casters.
             */
#if BRENDER_LEGACY_3DMM_MODEL_ABI
            if(v4dmm_shadow_traversal)
                break;
#endif
            if(BrOnScreenCheck(ap->type_data) == OSC_REJECT) {
                RendererStatePop(v1db.renderer, BR_STATE_MATRIX);
                return;
            }
            break;

        case BR_ACTOR_BOUNDS_CORRECT:
            /* BRender:
             * A garuanteed bounding box - test to see if it is on screen
             */
#if BRENDER_LEGACY_3DMM_MODEL_ABI
            if(v4dmm_shadow_traversal)
                break;
#endif
            switch(BrOnScreenCheck(ap->type_data)) {

                case OSC_ACCEPT:
                    /* BRender:
                     * Bounding box is completely on screen - process children with special loop
                     */
                    BR_FOR_SIMPLELIST(&ap->children, a)
                        actorRenderOnScreen(a, this_model, this_material, this_render_data, style, t);
                    /* BRender: FALL THROUGH */

                case OSC_REJECT:
                    /* BRender:
                     * Don't process children
                     */
                    RendererStatePop(v1db.renderer, BR_STATE_MATRIX);
                    return;
            }
    }

    /* BRender:
     * Recurse for children
     */
    BR_FOR_SIMPLELIST(&ap->children, a)
        actorRender(a, this_model, this_material, this_render_data, style, t);

    /* BRender:
     * Restore transforms
     */
    RendererStatePop(v1db.renderer, BR_STATE_MATRIX);
}

/* BRender:
 * Rendering traversal for an actor that is completely on screen, along
 * with any children
 */
static void actorRenderOnScreen(br_actor *ap, br_model *model, br_material *material, void *render_data, br_uint_8 style, br_uint_16 t)
{
    /* BRender:
     * Saved state
     */
    br_material *this_material;
    br_model    *this_model;
    void        *this_render_data;
    br_actor    *a;

    ASSERT_MESSAGE("Invalid actorRenderOnScreen pointer", model != NULL);
    ASSERT_MESSAGE("Invalid actorRenderOnScreen pointer", material != NULL);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    if(v4dmm_shadow_traversal && Br4DMMShadowExcludedRoot(ap)) {
        ++v4dmm_shadow_skipped_light_objects;
        return;
    }
#endif

    /* BRender:
     * See if this actor overrides default material, model or style
     */
    if(ap->render_style != BR_RSTYLE_DEFAULT)
        style = ap->render_style;

    if(style == BR_RSTYLE_NONE)
        return;

    this_material    = ap->material ? ap->material : material;
    this_model       = ap->model ? ap->model : model;
    this_render_data = ap->render_data ? ap->render_data : render_data;

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    /*
     * A shadow map is an immediate depth pass. 3DMM's render_data points at
     * order tables used by the ordinary colour renderer for Z-sorted/blended
     * primitives. Letting that state into the shadow traversal can defer a
     * caster instead of drawing it into the shadow FBO, and can leave those
     * deferred primitives behind for the following colour pass. Never inherit
     * actor/world order-table render data while building the shadow map.
     */
    if(v4dmm_shadow_traversal)
        this_render_data = NULL;
#endif

    /* BRender:
     * Catch special case of identity transforms
     */
    if(ap->t.type == BR_TRANSFORM_IDENTITY) {
        /* BRender:
         * This actor has no transform
         */
        if(ap->type == BR_ACTOR_MODEL) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
            if(Br4DMMShadowModelPolicySkip(ap)) {
                /* Keep walking children; this model belongs to the other shadow layer. */
            } else if(v4dmm_shadow_traversal && style == BR_RSTYLE_BOUNDING_EDGES) {
                ++v4dmm_shadow_skipped_edges;
            } else if(Br4DMMShadowNullModel(ap)) {
                /* Keep traversing children below; only this empty MODEL is absent. */
            } else
#endif
            {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                Br4DMMShadowTraceCaster(ap, this_model, style, OSC_ACCEPT);
#endif
                BrLightCullReset();
                BrDbModelRender(ap, this_model, this_material, this_render_data, style, OSC_ACCEPT, 1);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
                Br4DMMShadowModelRendered();
#endif
            }
        }

        BR_FOR_SIMPLELIST(&ap->children, a)
            actorRenderOnScreen(a, this_model, this_material, this_render_data, style, t);

        return;
    }

    /** BRender:
     ** Actor has a transform
     **/

    /* BRender:
     * Save the current transforms
     */
    RendererStatePush(v1db.renderer, BR_STATE_MATRIX);

    t = prependActorTransform(ap, t);

    if(ap->type == BR_ACTOR_MODEL) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        if(Br4DMMShadowModelPolicySkip(ap)) {
            /* Keep walking children; this model belongs to the other shadow layer. */
        } else if(v4dmm_shadow_traversal && style == BR_RSTYLE_BOUNDING_EDGES) {
            ++v4dmm_shadow_skipped_edges;
        } else if(Br4DMMShadowNullModel(ap)) {
            /* Keep traversing children below; only this empty MODEL is absent. */
        } else
#endif
        {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
            Br4DMMShadowTraceCaster(ap, this_model, style, OSC_ACCEPT);
#endif
            BrLightCullReset();
            BrDbModelRender(ap, this_model, this_material, this_render_data, style, OSC_ACCEPT, 1);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
            Br4DMMShadowModelRendered();
#endif
        }
    }

    BR_FOR_SIMPLELIST(&ap->children, a)
        actorRenderOnScreen(a, this_model, this_material, this_render_data, style, t);

    /* BRender:
     * Restore transforms
     */
    RendererStatePop(v1db.renderer, BR_STATE_MATRIX);
}

/* BRender:
 * Render all the children of root
 */
static void sceneRenderWorld(br_actor *world)
{
    br_model    *model       = v1db.default_model;
    br_material *material    = v1db.default_material;
    void        *render_data = v1db.default_render_data;
    br_uint_8    style       = BR_RSTYLE_DEFAULT;
    br_actor    *a;

    if(world->model)
        model = world->model;

    if(world->material)
        material = world->material;

    if(world->render_data)
        render_data = world->render_data;

    if(world->render_style != BR_RSTYLE_DEFAULT)
        style = world->render_style;

    BR_FOR_SIMPLELIST(&world->children, a)
        actorRender(a, model, material, render_data, style, (br_uint_16)v1db.ttype);
}

#if BRENDER_LEGACY_3DMM_MODEL_ABI
/*
 * v217 keeps the temporary "on-screen objects only" troubleshooting policy,
 * but applies it at the logical-object level instead of independently to every
 * BODY part. The first traversal performs main-camera visibility tests without
 * drawing and records visible owners. The second traversal renders every real
 * model part belonging to those owners into the shadow map. This makes the
 * caster silhouette stable while the camera moves across individual parts.
 */
static void sceneRenderWorldShadow(br_actor *world, br_boolean blocker_pass, br_boolean detail_pass)
{
    br_model    *model       = v1db.default_model;
    br_material *material    = v1db.default_material;
    void        *render_data = v1db.default_render_data;
    br_uint_8    style       = BR_RSTYLE_DEFAULT;
    br_actor    *a;

    if(world->model)
        model = world->model;
    if(world->material)
        material = world->material;
    if(world->render_data)
        render_data = world->render_data;
    if(world->render_style != BR_RSTYLE_DEFAULT)
        style = world->render_style;

    if(!blocker_pass && !detail_pass) {
        v4dmm_shadow_models = 0;
        v4dmm_shadow_skipped_edges = 0;
        v4dmm_shadow_skipped_light_objects = 0;
        v4dmm_shadow_skipped_null_models = 0;
        ++v4dmm_shadow_trace_frame;
        v4dmm_shadow_trace_draw = 0;

        if(v4dmm_shadow_trace_enabled) {
            BrWarning("SHADOW TRACE frame_begin frame=%u world=%p", (unsigned)v4dmm_shadow_trace_frame, world);
            BrWarning("SHADOW TRACE caster_policy_v219 frame=%u all_world=1 camera_culling=0",
                      (unsigned)v4dmm_shadow_trace_frame);
        }
    } else if(blocker_pass) {
        v4dmm_shadow_blocker_models = 0;
    } else {
        v4dmm_shadow_detail_models = 0;
    }

    /*
     * v233 uses two light-space geometry layers:
     *   blocker_pass=0: ordinary shadow casters.
     *   blocker_pass=1: only Object Properties "shadow casting 0" geometry.
     *
     * The second layer never creates a projected shadow. It records only the
     * first no-cast surface hit by a light ray so the colour shader can stop a
     * shadow that has already landed on that surface from tunnelling through
     * it to receivers farther down the same ray.
     */
    v4dmm_shadow_traversal = BR_TRUE;
    v4dmm_shadow_blocker_traversal = blocker_pass ? BR_TRUE : BR_FALSE;
    v4dmm_shadow_detail_traversal = detail_pass ? BR_TRUE : BR_FALSE;
    BR_FOR_SIMPLELIST(&world->children, a)
        actorRender(a, model, material, render_data, style, (br_uint_16)v1db.ttype);
    v4dmm_shadow_detail_traversal = BR_FALSE;
    v4dmm_shadow_blocker_traversal = BR_FALSE;
    v4dmm_shadow_traversal = BR_FALSE;
}

#endif

/* BRender:
 * Add a sub-tree to the current rendering pass -
 *
 * Walks up tree from provided actor to find the material, model and
 * style to inherit
 */
static void sceneRenderAdd(br_actor *tree)
{
    br_material *material    = NULL;
    br_model    *model       = NULL;
    void        *render_data = NULL;
    br_uint_8    style       = BR_RSTYLE_DEFAULT;
    br_actor    *a;
    br_int_32    t;
    br_matrix34  m;

    if(tree->parent == NULL) {
        /* BRender:
         * Simple case for when added tree is unconnected
         */
        actorRender(tree, v1db.default_model, v1db.default_material, v1db.default_render_data, BR_RSTYLE_DEFAULT, (br_uint_16)v1db.ttype);
        return;
    }

    t = BR_TRANSFORM_IDENTITY;
    BrMatrix34Identity(&m);

    /* BRender:
     * Walk back to current rendering root
     */
    for(a = tree->parent; a; a = a->parent) {
        /* BRender:
         * Closest material, model and render_data
         */
        if(material == NULL && a->material)
            material = a->material;

        if(model == NULL && a->model)
            model = a->model;

        if(render_data == NULL && a->render_data)
            render_data = a->render_data;
        /* BRender:
         * Furthest style
         */
        if(a->render_style != BR_RSTYLE_DEFAULT)
            style = a->render_style;

        /* BRender:
         * Quit if we have go the the root that is
         * being used for the current rendering pass
         * (before accumulating transform s.t. we do not
         * include root's transform
         */
        if(a == v1db.render_root)
            break;

        /* BRender:
         * Accumulate transform
         */
        if(a->t.type != BR_TRANSFORM_IDENTITY) {
            BrMatrix34PostTransform(&m, &a->t);
            t = BrTransformCombineTypes(t, a->t.type);
        }
    }

    if(material == NULL)
        material = v1db.default_material;

    if(model == NULL)
        model = v1db.default_model;

    if(render_data == NULL)
        render_data = v1db.default_render_data;

    if(t == BR_TRANSFORM_IDENTITY) {
        actorRender(tree, model, material, render_data, style, (br_uint_16)v1db.ttype);
    } else {
        RendererStatePush(v1db.renderer, BR_STATE_MATRIX);

        t = prependMatrix(&m, (br_uint_16)t, (br_uint_16)v1db.ttype);

        actorRender(tree, model, material, render_data, style, (br_uint_16)t);

        RendererStatePop(v1db.renderer, BR_STATE_MATRIX);
    }
}

/* BRender: Signal start of frame */
void BR_PUBLIC_ENTRY BrRendererFrameBegin(void)
{
    RendererFrameBegin(v1db.renderer);
}

/* BRender: Signal end of frame */
void BR_PUBLIC_ENTRY BrRendererFrameEnd(void)
{
    RendererFrameEnd(v1db.renderer);
}

/* BRender: Hack to tell d3d driver when it is about to loose surfaces */
void BR_PUBLIC_ENTRY BrRendererFocusLossBegin(void)
{
    if(v1db.renderer)
        RendererFocusLossBegin(v1db.renderer);
}

/* BRender: Hack to tell d3d driver when it will have to restore surfaces */
void BR_PUBLIC_ENTRY BrRendererFocusLossEnd(void)
{
    if(v1db.renderer)
        RendererFocusLossEnd(v1db.renderer);
}

/* BRender:
 * BrDbSceneRenderBegin()
 *
 * Setup a new scene to be rendered - processes the camera, lights
 * and environment
 */
void BR_PUBLIC_ENTRY BrDbSceneRenderBegin(br_actor *world, br_actor *camera)
{
    br_matrix34  tfm;
    br_matrix4   vtos;
    br_actor    *a;
    int          i;
    br_token     vtos_type;
    br_uintptr_t dummy;

    UASSERT_MESSAGE("No renderer present", v1db.renderer != NULL);
    UASSERT_MESSAGE("Invalid BrDbSceneRenderBegin pointer", world != NULL);
    UASSERT_MESSAGE("Invalid BrDbSceneRenderBegin pointer", camera != NULL);

    /* BRender:
     * Work out View Transform from info. in camera actor
     */
    vtos_type = CameraToScreenMatrix4(&vtos, camera);
    RendererPartSet(v1db.renderer, BRT_MATRIX, 0, BRT_AS_MATRIX4_SCALAR(VIEW_TO_SCREEN), (br_value){.m4 = &vtos});
    RendererPartSet(v1db.renderer, BRT_MATRIX, 0, BRT_VIEW_TO_SCREEN_HINT_T, (br_value){.t = vtos_type});

    RendererPartSet(v1db.renderer, BRT_MATRIX, 0, BRT_AS_SCALAR(HITHER_Z), (br_value){.s = ((br_camera *)camera->type_data)->hither_z});
    RendererPartSet(v1db.renderer, BRT_MATRIX, 0, BRT_AS_SCALAR(YON_Z), (br_value){.s = ((br_camera *)camera->type_data)->yon_z});

    /* BRender:
     * Collect transforms from camera to root
     *
     * Make a stack of cumulative transforms for each level between
     * the camera and the root - this is so that model->view
     * transforms can use the shortest route, rather than via the root
     */
    for(i = 0; i < MAX_CAMERA_DEPTH; i++)
        v1db.camera_path[i].a = NULL;

    i = camera->depth;
    a = camera;
    BrMatrix34Identity(&v1db.camera_path[i].m);
    v1db.camera_path[i].transform_type = BR_TRANSFORM_IDENTITY;

    for(; (i > 0) && (a != world); a = a->parent, i--) {
        ASSERT(a != NULL);
        BrMatrix34Transform(&tfm, &a->t);
        BrMatrix34Mul(&v1db.camera_path[i - 1].m, &v1db.camera_path[i].m, &tfm);

        v1db.camera_path[i - 1].transform_type = BrTransformCombineTypes(v1db.camera_path[i].transform_type, a->t.type);

        v1db.camera_path[i].a = a;
    }

    if(world != a)
        BR_ERROR0("camera is not in world hierachy");

    /* BRender:
     * Make world->view as initial model->view
     */
    RendererPartSet(v1db.renderer, BRT_MATRIX, 0, BRT_AS_MATRIX34_SCALAR(MODEL_TO_VIEW), (br_value){.m34 = &v1db.camera_path[i].m});

    v1db.ttype = v1db.camera_path[i].transform_type;

    RendererPartSet(v1db.renderer, BRT_MATRIX, 0, BRT_MODEL_TO_VIEW_HINT_T,
                    (br_value){.t = BrTransformTypeIsLP(v1db.ttype) ? BRT_LENGTH_PRESERVING : BRT_NONE});

    RendererModelInvert(v1db.renderer);

    /* BRender:
     * Setup active lights, clip planes, horizon and environment
     */
    RendererPartQueryBuffer(v1db.renderer, BRT_MATRIX, 0, &dummy, (br_uint_32 *)&tfm, sizeof(tfm), BRT_AS_MATRIX34_SCALAR(MODEL_TO_VIEW));

    BrSetupLights(world, camera, &tfm, v1db.ttype);
    BrSetupClipPlanes(world, &tfm, v1db.ttype, &vtos);
    BrSetupEnvironment(world, &tfm, v1db.ttype);
    BrSetupHorizons(world, &tfm, v1db.ttype);

    RendererSceneBegin(v1db.renderer);
}

/* BRender:
 * Set a callback function for bounding rectangles
 */
br_renderbounds_cbfn *BR_PUBLIC_ENTRY BrDbSetRenderBoundsCallback(br_renderbounds_cbfn *new_cbfn)
{
    br_renderbounds_cbfn *old_cbfn = v1db.bounds_call;
    UASSERT_MESSAGE("Invalid BrDbSetRenderBoundsCallback pointer", new_cbfn != NULL);
    v1db.bounds_call = new_cbfn;

    /* BRender:
     * Enable or disable bounds in renderer
     */
    if(v1db.renderer)
        RendererPartSet(v1db.renderer, BRT_ENABLE, 0, BRT_BOUNDS_B, (br_value){.b = (v1db.bounds_call != NULL)});

    return old_cbfn;
}

/* BRender:
 * Compatibility functions
 */
static void SetOrigin(br_pixelmap *buffer)
{
    ASSERT(buffer != NULL);
    ASSERT_MESSAGE("SetOrigin divide by zero error", BrIntToScalar(buffer->width / 2) != 0);
    ASSERT_MESSAGE("SetOrigin divide by zero error", BrIntToScalar(buffer->height / 2) != 0);

    v1db.origin.v[0] = BR_DIV(BrIntToScalar(buffer->origin_x - buffer->width / 2), BrIntToScalar(buffer->width / 2));

    v1db.origin.v[1] = -BR_DIV(BrIntToScalar(buffer->origin_y - buffer->height / 2), BrIntToScalar(buffer->height / 2));
}

static void SetViewport(br_pixelmap *buffer)
{
    ASSERT(buffer != NULL);
    v1db.vp_ox    = BR_SCALAR(buffer->base_x + buffer->width / 2) + BR_SCALAR(0.5);
    v1db.vp_width = BR_SCALAR(buffer->width / 2);

    v1db.vp_oy     = BR_SCALAR(buffer->height / 2) + BR_SCALAR(0.5);
    v1db.vp_height = -BR_SCALAR(buffer->height / 2);
};

void BR_PUBLIC_ENTRY BrZbSceneRenderBegin(br_actor *world, br_actor *camera, br_pixelmap *colour_buffer, br_pixelmap *depth_buffer)
{
    br_camera *camera_data;

    UASSERT_MESSAGE("No renderer present", v1db.renderer != NULL);
    UASSERT(v1db.rendering == RENDERING_NONE);
    UASSERT_MESSAGE("Invalid BrZbSceneRenderBegin actor pointer", world != NULL);
    UASSERT_MESSAGE("Invalid BrZbSceneRenderBegin actor pointer", camera != NULL);
    UASSERT_MESSAGE("Invalid BrZbSceneRenderBegin pixelmap pointer", colour_buffer != NULL);
    UASSERT_MESSAGE("Invalid BrZbSceneRenderBegin pixelmap pointer", depth_buffer != NULL);

    v1db.rendering     = RENDERING_ZB;
    v1db.render_root   = world;
    v1db.colour_buffer = colour_buffer;

    /* BRender:
     * Setup output state for renderer
     */
    SetOrigin(colour_buffer);
    SetViewport(colour_buffer);

    RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, BRT_COLOUR_BUFFER_O, (br_value){.o = (br_object *)colour_buffer});
    RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, BRT_DEPTH_BUFFER_O, (br_value){.o = (br_object *)depth_buffer});

    /* BRender:
     * Setup primitives heap and order table for deferred primitives
     */
    if(v1db.format_buckets != NULL) {

        v1db.heap.current = v1db.heap.base;

        camera_data = (br_camera *)camera->type_data;

        v1db.default_order_table->max_z  = camera_data->yon_z;
        v1db.default_order_table->min_z  = camera_data->hither_z;
        v1db.default_order_table->visits = 0;

        v1db.order_table_list = NULL;

        RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_V1ORDER_TABLE_P, (br_value){.p = v1db.default_order_table});
        RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_V1PRIMITIVE_HEAP_P, (br_value){.p = &v1db.heap});
        RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_TYPE_T, (br_value){.t = BRT_BUCKET_SORT});
        RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_DIVERT_T, (br_value){.t = BRT_BLENDED});

        v1db.default_render_data = v1db.default_order_table;

    } else {

        RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_TYPE_T, (br_value){.t = BRT_NONE});
        RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_DIVERT_T, (br_value){.t = BRT_NONE});

        v1db.default_render_data = NULL;
    }

    BrDbSceneRenderBegin(world, camera);
}

void BR_PUBLIC_ENTRY BrZbSceneRenderContinue(br_actor *world, br_actor *camera, br_pixelmap *colour_buffer, br_pixelmap *depth_buffer)
{
    br_camera *camera_data;

    UASSERT(v1db.rendering == RENDERING_ZB);
    UASSERT_MESSAGE("Invalid BrZbSceneRenderBegin actor pointer", world != NULL);
    UASSERT_MESSAGE("Invalid BrZbSceneRenderBegin actor pointer", camera != NULL);
    UASSERT_MESSAGE("Invalid BrZbSceneRenderBegin pixelmap pointer", colour_buffer != NULL);
    UASSERT_MESSAGE("Invalid BrZbSceneRenderBegin pixelmap pointer", depth_buffer != NULL);

    v1db.render_root   = world;
    v1db.colour_buffer = colour_buffer;

    /* BRender:
     * Setup output state for renderer
     */
    SetOrigin(colour_buffer);
    SetViewport(colour_buffer);

    RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, BRT_COLOUR_BUFFER_O, (br_value){.o = (br_object *)colour_buffer});
    RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, BRT_DEPTH_BUFFER_O, (br_value){.o = (br_object *)depth_buffer});

    BrDbSceneRenderBegin(world, camera);
}

void BR_PUBLIC_ENTRY BrZbSceneRenderAdd(br_actor *tree)
{
    UASSERT(v1db.rendering == RENDERING_ZB);
    UASSERT_MESSAGE("Invalid BrZbSceneRenderAdd pointer", tree != NULL);
    sceneRenderAdd(tree);
}

void BR_PUBLIC_ENTRY BrZbSceneRenderEnd(void)
{
    UASSERT(v1db.rendering == RENDERING_ZB);

    /* BRender:
     * Draw any deferred primitives
     */
    if(v1db.format_buckets != NULL) {

        if(v1db.primary_order_table) {

            /* BRender:
             * Render primitives in the primary order table
             * and the list of order tables
             */
            RenderPrimaryOrderTable();
        } else {
            /* BRender:
             * Render primitives in the list of order table
             */
            RenderOrderTableList();
        }
    }

    /* BRender:
     * Tell the renderer to flush
     */
    RendererFlush(v1db.renderer, BR_FALSE);
    RendererSceneEnd(v1db.renderer);

    v1db.rendering   = RENDERING_NONE;
    v1db.render_root = NULL;
}

void BR_PUBLIC_ENTRY BrZbSceneRender(br_actor *world, br_actor *camera, br_pixelmap *colour_buffer, br_pixelmap *depth_buffer)
{
    UASSERT_MESSAGE("No renderer present", v1db.renderer != NULL);
    UASSERT_MESSAGE("Invalid BrZbSceneRender actor pointer", world != NULL);
    UASSERT_MESSAGE("Invalid BrZbSceneRender actor pointer", camera != NULL);
    UASSERT_MESSAGE("Invalid BrZbSceneRender pixelmap pointer", colour_buffer != NULL);
    UASSERT_MESSAGE("Invalid BrZbSceneRender pixelmap pointer", depth_buffer != NULL);

    // BRender: Stop BRender dying horribly when no renderer is loaded

    if(v1db.renderer) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        br_boolean shadow_pass = Br4DMMShadowLightEnabled();
        br_token shadow_token = BR_NULL_TOKEN;
        br_token shadow_blocker_token = BR_NULL_TOKEN;
        br_token shadow_detail_token = BR_NULL_TOKEN;
        if(shadow_pass) {
            shadow_token = Br4DMMShadowPassToken();
            shadow_blocker_token = Br4DMMShadowBlockerPassToken();
            shadow_detail_token = Br4DMMShadowDetailPassToken();
            (void)RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, shadow_blocker_token,
                                  (br_value){.b = BR_FALSE});
            (void)RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, shadow_detail_token,
                                  (br_value){.b = BR_FALSE});
            shadow_pass = RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, shadow_token,
                                          (br_value){.b = BR_TRUE}) == BRE_OK;
        }
#endif

#if BRENDER_LEGACY_3DMM_MODEL_ABI
        Br4DMMShadowOwnerSlotsReset();
#endif
        BrZbSceneRenderBegin(world, camera, colour_buffer, depth_buffer);

#if BRENDER_LEGACY_3DMM_MODEL_ABI
        if(shadow_pass) {
            static br_uint_32 shadow_core_diag;
            const br_uint_32 shadow_diag_id = shadow_core_diag++;
            if(shadow_diag_id < 96)
                BrWarning("SHADOW core depth_scene_begin n=%u world=%p camera=%p colour=%p depth=%p self_shadow_v269=depth_separation_64lsb",
                          (unsigned)shadow_diag_id, world, camera, colour_buffer, depth_buffer);

            /*
             * Keep the light-space depth traversal in its own renderer
             * scene. v203 left the shadow FBO/program/depth state active
             * until the first ordinary model happened to call apply_state(),
             * which let non-model/immediate paths inherit shadow state.
             */
            /*
             * The normal 3DMM Z-buffer scene diverts blended primitives into
             * an order table. A shadow map must never use that path: every
             * caster has to hit the depth FBO immediately in this traversal.
             * Temporarily disable hidden-surface/order-table diversion, then
             * restore the original ZB policy before the colour scene begins.
             */
            RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_TYPE_T,
                            (br_value){.t = BRT_NONE});
            RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_DIVERT_T,
                            (br_value){.t = BRT_NONE});

            sceneRenderWorldShadow(world, BR_FALSE, BR_FALSE);
            if(shadow_diag_id < 96)
                BrWarning("SHADOW depth_submit models=%u owner_slots=%u owner_format=r8ui_v227 overflow_slot=255 skipped_edges=%u skipped_light_objects=%u skipped_null_models=%u direct_depth=1 owner_tagged=1 self_shadow=v269_same_owner_64lsb caster_policy_v219=all_world camera_culling=0 direct_actor_to_light_v220=1",
                          (unsigned)v4dmm_shadow_models, (unsigned)v4dmm_shadow_owner_key_count,
                          (unsigned)v4dmm_shadow_skipped_edges,
                          (unsigned)v4dmm_shadow_skipped_light_objects,
                          (unsigned)v4dmm_shadow_skipped_null_models);
            RendererFlush(v1db.renderer, BR_FALSE);

            /*
             * v233: render Object Properties "shadow casting 0" geometry into
             * a separate depth-only stopper layer.  It must not enter the
             * ordinary caster map, otherwise the object would cast its own
             * shadow again.  The colour shader uses this layer only to stop an
             * existing caster shadow after it lands on the no-cast object.
             */
            (void)RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, shadow_blocker_token,
                                  (br_value){.b = BR_TRUE});
            sceneRenderWorldShadow(world, BR_TRUE, BR_FALSE);
            RendererFlush(v1db.renderer, BR_FALSE);
            if(shadow_diag_id < 96)
                BrWarning("SHADOW blocker_submit_v233 models=%u depth_only=1 semantics=stop_incoming_shadow_no_own_cast=1",
                          (unsigned)v4dmm_shadow_blocker_models);

            /*
             * v237 nested detail cascade. Render ordinary casters a second time
             * into the optional depth-only detail target. The base map remains
             * complete coverage and supplies owner/blocker semantics.
             */
            (void)RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, shadow_blocker_token,
                                  (br_value){.b = BR_FALSE});
            (void)RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, shadow_detail_token,
                                  (br_value){.b = BR_TRUE});
            sceneRenderWorldShadow(world, BR_FALSE, BR_TRUE);
            RendererFlush(v1db.renderer, BR_FALSE);
            if(shadow_diag_id < 96)
                BrWarning("SHADOW detail_submit_v237 models=%u nested_depth_only=1 base_fallback=1",
                          (unsigned)v4dmm_shadow_detail_models);
            (void)RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, shadow_detail_token,
                                  (br_value){.b = BR_FALSE});

            (void)RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, shadow_token, (br_value){.b = BR_FALSE});
            RendererSceneEnd(v1db.renderer);

            if(shadow_diag_id < 96)
                BrWarning("SHADOW core depth_scene_end n=%u; restarting isolated colour scene",
                          (unsigned)shadow_diag_id);

            if(v1db.format_buckets != NULL) {
                RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_V1ORDER_TABLE_P,
                                (br_value){.p = v1db.default_order_table});
                RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_V1PRIMITIVE_HEAP_P,
                                (br_value){.p = &v1db.heap});
                RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_TYPE_T,
                                (br_value){.t = BRT_BUCKET_SORT});
                RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_DIVERT_T,
                                (br_value){.t = BRT_BLENDED});
            } else {
                RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_TYPE_T,
                                (br_value){.t = BRT_NONE});
                RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_DIVERT_T,
                                (br_value){.t = BRT_NONE});
            }

            BrDbSceneRenderBegin(world, camera);

            if(shadow_diag_id < 96)
                BrWarning("SHADOW core colour_scene_begin n=%u", (unsigned)shadow_diag_id);
        }
#endif

        sceneRenderWorld(world);

        BrZbSceneRenderEnd();
    }
}

void BR_PUBLIC_ENTRY BrZsSceneRenderBegin(br_actor *world, br_actor *camera, br_pixelmap *colour_buffer)
{
    br_camera *camera_data;

    UASSERT(v1db.renderer != NULL);
    UASSERT(v1db.rendering == RENDERING_NONE);
    UASSERT_MESSAGE("Invalid BrZsSceneRenderBegin actor pointer", world != NULL);
    UASSERT_MESSAGE("Invalid BrZsSceneRenderBegin actor pointer", camera != NULL);
    UASSERT_MESSAGE("Invalid BrZsSceneRenderBegin pixelmap pointer", colour_buffer != NULL);

    v1db.rendering     = RENDERING_ZS;
    v1db.render_root   = world;
    v1db.colour_buffer = colour_buffer;

    SetOrigin(colour_buffer);
    SetViewport(colour_buffer);

    RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, BRT_COLOUR_BUFFER_O, (br_value){.o = (br_object *)colour_buffer});
    RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, BRT_DEPTH_BUFFER_O, (br_value){.o = NULL});

    /* BRender:
     * Setup primitives heap and order table
     */
    v1db.heap.current = v1db.heap.base;

    camera_data = (br_camera *)camera->type_data;

    v1db.default_order_table->max_z  = camera_data->yon_z;
    v1db.default_order_table->min_z  = camera_data->hither_z;
    v1db.default_order_table->visits = 0;

    v1db.order_table_list = NULL;

    RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_V1ORDER_TABLE_P, (br_value){.p = v1db.default_order_table});
    RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_V1PRIMITIVE_HEAP_P, (br_value){.p = &v1db.heap});
    RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_TYPE_T, (br_value){.t = BRT_BUCKET_SORT});
    RendererPartSet(v1db.renderer, BRT_HIDDEN_SURFACE, 0, BRT_DIVERT_T, (br_value){.t = BRT_ALL});

    v1db.default_render_data = v1db.default_order_table;

    BrDbSceneRenderBegin(world, camera);
}

void BR_PUBLIC_ENTRY BrZsSceneRenderContinue(br_actor *world, br_actor *camera, br_pixelmap *colour_buffer)
{
    UASSERT(v1db.rendering == RENDERING_ZS);
    UASSERT_MESSAGE("Invalid BrZsSceneRenderBegin actor pointer", world != NULL);
    UASSERT_MESSAGE("Invalid BrZsSceneRenderBegin actor pointer", camera != NULL);
    UASSERT_MESSAGE("Invalid BrZsSceneRenderBegin pixelmap pointer", colour_buffer != NULL);

    v1db.render_root   = world;
    v1db.colour_buffer = colour_buffer;

    SetOrigin(colour_buffer);
    SetViewport(colour_buffer);

    RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, BRT_COLOUR_BUFFER_O, (br_value){.o = (br_object *)colour_buffer});
    RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, BRT_DEPTH_BUFFER_O, (br_value){.o = NULL});

    BrDbSceneRenderBegin(world, camera);
}

void BR_PUBLIC_ENTRY BrZsSceneRenderAdd(br_actor *tree)
{
    UASSERT(v1db.rendering == RENDERING_ZS);
    UASSERT_MESSAGE("Invalid BrZsSceneRenderAdd pointer", tree != NULL);
    sceneRenderAdd(tree);
}

void BR_PUBLIC_ENTRY BrZsSceneRenderEnd(void)
{
    UASSERT(v1db.rendering == RENDERING_ZS);

    RendererPartSet(v1db.renderer, BRT_OUTPUT, 0, BRT_COLOUR_BUFFER_O, (br_value){.o = (br_object *)v1db.colour_buffer});

    if(v1db.format_buckets == NULL)
        BR_ERROR0("Renderer does not support buckets");

    if(v1db.primary_order_table) {

        /* BRender:
         * Render primitives in the primary order table
         * and the list of order tables
         */
        RenderPrimaryOrderTable();
    } else {
        /* BRender:
         * Render primitives in the list of order table
         */
        RenderOrderTableList();
    }

    /* BRender:
     * Tell the renderer to flush
     */
    RendererFlush(v1db.renderer, BR_FALSE);
    RendererSceneEnd(v1db.renderer);

    v1db.rendering   = RENDERING_NONE;
    v1db.render_root = NULL;
}

/* BRender:
 * Wrapper that invokes above three calls in order
 */
void BR_PUBLIC_ENTRY BrZsSceneRender(br_actor *world, br_actor *camera, br_pixelmap *colour_buffer)
{
    UASSERT_MESSAGE("No renderer present", v1db.renderer != NULL);
    UASSERT_MESSAGE("Invalid BrZsSceneRender actor pointer", world != NULL);
    UASSERT_MESSAGE("Invalid BrZsSceneRender actor pointer", camera != NULL);
    UASSERT_MESSAGE("Invalid BrZsSceneRender pixelmap pointer", colour_buffer != NULL);

    // BRender: Stop BRender dying horribly when no renderer present.

    if(v1db.renderer) {
        BrZsSceneRenderBegin(world, camera, colour_buffer);
        sceneRenderWorld(world);
        BrZsSceneRenderEnd();
    }
}

br_primitive_cbfn *BR_PUBLIC_ENTRY BrZsPrimitiveCallbackSet(br_primitive_cbfn *new_cbfn)
{
    br_primitive_cbfn *old_cbfn = v1db.primitive_call;
    v1db.primitive_call         = new_cbfn;

    return old_cbfn;
}

void BR_PUBLIC_ENTRY BrZbModelRender(br_actor *actor, br_model *model, br_material *material, br_uint_8 style, int on_screen, int use_custom)
{

    UASSERT_MESSAGE("Invalid BrZbModelRender model actor pointer", actor != NULL);
    UASSERT_MESSAGE("Invalid BrZbModelRender model pointer", model != NULL);
    UASSERT_MESSAGE("Invalid BrZbModelRender material pointer", material != NULL);

    BrDbModelRender(actor, model, material, NULL, style, on_screen, use_custom);
}

void BR_PUBLIC_ENTRY BrZsModelRender(br_actor *actor, br_model *model, br_material *material, br_order_table *order_table, br_uint_8 style,
                                     int on_screen, int use_custom)
{
    UASSERT_MESSAGE("Invalid BrZsModelRender model actor pointer", actor != NULL);
    UASSERT_MESSAGE("Invalid BrZsModelRender model pointer", model != NULL);
    UASSERT_MESSAGE("Invalid BrZsModelRender material pointer", material != NULL);
    UASSERT_MESSAGE("Invalid BrZsModelRender order table pointer", order_table != NULL);

    BrDbModelRender(actor, model, material, order_table, style, on_screen, use_custom);
}

br_renderbounds_cbfn *BR_PUBLIC_ENTRY BrZbRenderBoundsCallbackSet(br_renderbounds_cbfn *new_cbfn)
{
    ASSERT(new_cbfn != NULL);

    if(!v1db.zb_active) {
        BR_ERROR0("BrZbSetRenderBoundsCallback called before BrZbBegin");
        return NULL;
    }

    return BrDbSetRenderBoundsCallback(new_cbfn);
}

br_renderbounds_cbfn *BR_PUBLIC_ENTRY BrZsRenderBoundsCallbackSet(br_renderbounds_cbfn *new_cbfn)
{
    ASSERT(new_cbfn != NULL);

    if(!v1db.zs_active) {
        BR_ERROR0("BrZsSetRenderBoundsCallback called before BrZsBegin");
        return NULL;
    }

    return BrDbSetRenderBoundsCallback(new_cbfn);
}
