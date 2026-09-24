#include "drv.h"
#include "brassert.h"
#include "shortcut.h"
#include "vecifns.h"

static int light_type_to_int(br_token type)
{
    switch(type) {
        case BRT_AMBIENT:
            return 0;
        case BRT_DIRECT:
            return 1;
        case BRT_POINT:
            return 2;
        case BRT_SPOT:
            return 3;
        default:
            return 100;
    }
}

static int sort_lights(const void *a, const void *b)
{
    const state_light *l1    = a;
    const state_light *l2    = b;
    const int          type1 = light_type_to_int(l1->type);
    const int          type2 = light_type_to_int(l2->type);

    if(type1 < type2)
        return -1;

    if(type1 > type2)
        return 1;

    return 0;
}

/* BRenderModern:
 * -1 = completely ignore this light.
 *  0 = ok
 */
static int casfd(state_cache *cache, const state_light *in, size_t i, br_vector4_i *counts, br_boolean *use_ambient_colour)
{
    br_gl_main_data_scene *scene = &cache->scene;
    if(in->type == BRT_NONE)
        return -1;

    br_float intensity = 16384.0f; /* BRenderModern: Effectively infinite */
    const br_float attenuation_c = BrGLScalarToFloat(in->attenuation_c);
    if(attenuation_c != 0.0f)
        intensity = 1.0f / attenuation_c;

    br_vector4_f colour;
    BrGLVector4FColourSet(&colour, in->colour);

    /* BRenderModern:
     * Regular (i.e. non-radial) ambient lights can be accumulated early,
     * moving work out of the shader.
     */
    if(in->type == BRT_AMBIENT && in->attenuation_type != BRT_RADII) {
        BrGLVector4FAccumulateScale(&scene->ambient_colour, &colour, intensity);
        *use_ambient_colour = BR_TRUE;
        return -1;
    }

    br_gl_main_data_light_info  *info  = scene->light_info + i;
    br_gl_main_data_light_atten *atten = scene->light_atten + i;
    br_gl_main_data_light_radii *radii = scene->light_radii + i;

    switch(in->type) {
        case BRT_AMBIENT:
            info->type = 0;
            break;
        case BRT_DIRECT:
            info->type = 1;
            break;
        case BRT_POINT:
            info->type = 2;
            break;
        case BRT_SPOT:
            info->type = 3;
            break;
        default:
            return -1;
    }

    counts->v[info->type]++;

    /* BRenderModern: See enables.c:194, BrSetupLights(). All the lights are already converted into view space. */
    BrGLVector4FSet(scene->light_positions + i, in->position.v[0], in->position.v[1], in->position.v[2], in->type == BRT_DIRECT ? 0.0f : 1.0f);
    BrGLVector4FSet(scene->light_directions + i, in->direction.v[0], in->direction.v[1], in->direction.v[2], 0.0f);

    if(in->type == BRT_DIRECT) {
        scene->light_halfs[i] = scene->light_directions[i];
        scene->light_halfs[i].v[2] += 1.0f;
        BrGLVector4FNormalise(scene->light_halfs + i, scene->light_halfs + i);

        BrGLVector4FScale(scene->light_directions + i, scene->light_directions + i, intensity);
    }

    atten->intensity     = intensity;
    atten->attenuation_c = attenuation_c;
    atten->attenuation_l = BrGLScalarToFloat(in->attenuation_l);
    atten->attenuation_q = BrGLScalarToFloat(in->attenuation_q);

    scene->light_colours[i] = colour;

    if(in->type == BRT_SPOT) {
        radii->spot_cos_inner = BrGLScalarToFloat(in->spot_inner);
        radii->spot_cos_outer = BrGLScalarToFloat(in->spot_outer);
    } else {
        radii->spot_cos_inner = 0.0f;
        radii->spot_cos_outer = 0.0f;
    }

    switch(in->attenuation_type) {
        case BRT_QUADRATIC:
        default:
            info->attenuation_type = 0;
            break;

        case BRT_RADII:
            info->attenuation_type = 1;

            radii->radius_inner = BrGLScalarToFloat(in->radius_inner);
            radii->radius_outer = BrGLScalarToFloat(in->radius_outer);

            if(radii->radius_inner == radii->radius_outer)
                radii->radius_inner = 0;

            break;
    }

    if(in->shadow && in->type == BRT_SPOT && scene->shadow_info.v[0] == 0.0f) {
        const br_float cos_outer = radii->spot_cos_outer;
        const br_float sin_sq    = 1.0f - cos_outer * cos_outer;
        const br_float sin_outer = sqrtf(sin_sq > 0.0f ? sin_sq : 0.0f);
        const br_float cos_safe  = cos_outer > 0.001f ? cos_outer : 0.001f;
        const br_float tan_half  = sin_outer / cos_safe;
        const br_float far_z     = radii->radius_outer > 1.0f ? radii->radius_outer : 500.0f;
        const br_float near_calc = far_z * 0.001f;
        const br_float near_z    = near_calc > 0.5f ? near_calc : 0.5f;

        BrGLVector4FSet(&scene->shadow_info, 1.0f, (br_float)i, tan_half, near_z);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        {
            /*
             * v230: StateGLUpdateScene() has already put world +Y in view
             * space into shadow_world_up_view.  Convert that vector into the
             * selected shadow light's local space before v228 reuses
             * shadow_world_up_view for fitted projection coordinates.
             * shadow_world_right_view was unused by the direct light-local
             * path, so it now carries world +Y in light-local space.
             */
            br_vector3 world_up_view;
            br_vector3 world_up_light;

            BrVector3Set(&world_up_view,
                         BrFloatToScalar(scene->shadow_world_up_view.v[0]),
                         BrFloatToScalar(scene->shadow_world_up_view.v[1]),
                         BrFloatToScalar(scene->shadow_world_up_view.v[2]));
            BrMatrix34ApplyV(&world_up_light, &world_up_view, &in->shadow_view_to_light);
            BrGLVector4FSet(&scene->shadow_world_right_view,
                            BrScalarToFloat(world_up_light.v[0]),
                            BrScalarToFloat(world_up_light.v[1]),
                            BrScalarToFloat(world_up_light.v[2]), 0.0f);
        }

        /*
         * v254 adaptive detail cascade. Keep the v228 base projection as
         * guaranteed all-world coverage. The second depth-only map follows the
         * camera-visible caster fit computed by v1db, so moving a visible actor
         * around the spotlight does not kick it out of the high-density region.
         * If v1db cannot derive a useful visible-caster fit, retain the proven
         * v237 central-quarter fallback. The colour shader always falls back to
         * the base map outside the detail crop.
         */
        {
            br_float center_u = 0.0f, center_v = 0.0f;
            br_float base_half_u = tan_half, base_half_v = tan_half;
            br_float detail_center_u, detail_center_v;
            br_float detail_half_u, detail_half_v;
            br_float detail_zoom_u, detail_zoom_v;

            if(in->shadow_fit.v[2] > 0.0f && in->shadow_fit.v[3] > 0.0f) {
                center_u = in->shadow_fit.v[0];
                center_v = in->shadow_fit.v[1];
                base_half_u = in->shadow_fit.v[2];
                base_half_v = in->shadow_fit.v[3];
            }

            detail_center_u = center_u;
            detail_center_v = center_v;
            detail_half_u = base_half_u * 0.25f;
            detail_half_v = base_half_v * 0.25f;

            if(in->shadow_detail_fit.v[2] > 0.0f && in->shadow_detail_fit.v[3] > 0.0f) {
                detail_center_u = in->shadow_detail_fit.v[0];
                detail_center_v = in->shadow_detail_fit.v[1];
                detail_half_u = in->shadow_detail_fit.v[2];
                detail_half_v = in->shadow_detail_fit.v[3];
            }

            detail_zoom_u = detail_half_u > 0.0f ? base_half_u / detail_half_u : 1.0f;
            detail_zoom_v = detail_half_v > 0.0f ? base_half_v / detail_half_v : 1.0f;

            BrGLVector4FSet(&cache->scene.shadow_world_up_view,
                            center_u, center_v, base_half_u, base_half_v);
            BrGLVector4FSet(&cache->scene.shadow_detail_fit,
                            detail_center_u, detail_center_v, detail_half_u, detail_half_v);
            BrGLVector4FSet(&cache->scene.shadow_detail_info,
                            1.0f, 0.0f,
                            detail_zoom_u < detail_zoom_v ? detail_zoom_u : detail_zoom_v,
                            0.0f);
        }
#endif
        /*
         * v223: keep the v222 contact-shadow fix, but add only a four-LSB
         * guard for the actual 24-bit shadow depth target. This is tiny enough
         * to preserve caster/ground contact while preventing two different
         * owners on the same mathematical plane from shadowing each other due
         * solely to depth quantisation noise.
         */
        BrGLVector4FSet(&scene->shadow_info2, far_z, BR_GLREND_SHADOW_DEPTH24_BIAS, 0.0f, 0.0f);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        BrMatrix34Copy(&cache->shadow_view_to_light, &in->shadow_view_to_light);
        cache->shadow_view_to_light_valid = BR_TRUE;
#endif
    }

    return 0;
}

/* BRenderModern:
** Process each light, doing as much once-per-frame work as possible.
** - For work that cannot be done here, see GLSTATE_ProcessActiveLights()
*/
static void ProcessSceneLights(state_cache *cache, state_light *lights)
{
    br_gl_main_data_scene *scene              = &cache->scene;
    size_t                 num_lights         = 0;
    br_boolean             use_ambient_colour = BR_FALSE;

    br_vector4_i counts = {
        .v = {0, 0, 0, 0},
    };

    /* BRenderModern:
     * Sort the lights first - ambient < direct < point < spot.
     */
    BrQsort(lights, MAX_STATE_LIGHTS, sizeof(state_light), sort_lights);

    BrGLVector4FColourSet(&scene->ambient_colour, BR_COLOUR_RGBA(0, 0, 0, 0xFF));
    BrGLVector4FSet(&scene->shadow_info, 0.0f, -1.0f, 0.0f, 0.0f);
    BrGLVector4FSet(&scene->shadow_info2, 0.0f, 0.0f, 0.0f, 0.0f);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
    cache->shadow_view_to_light_valid = BR_FALSE;
#endif

    for(uint32_t i = 0; i < MAX_STATE_LIGHTS; ++i) {
        const state_light *light = lights + i;

        if(casfd(cache, light, num_lights, &counts, &use_ambient_colour) < 0)
            continue;

        ++num_lights;
    }

    scene->light_start.v[0] = 0;
    scene->light_end.v[0]   = counts.v[0];

    scene->light_start.v[1] = scene->light_end.v[0];
    scene->light_end.v[1]   = scene->light_start.v[1] + counts.v[1];

    scene->light_start.v[2] = scene->light_end.v[1];
    scene->light_end.v[2]   = scene->light_start.v[2] + counts.v[2];

    scene->light_start.v[3] = scene->light_end.v[2];
    scene->light_end.v[3]   = scene->light_start.v[3] + counts.v[3];

    if(use_ambient_colour) {
        BrGLVector4FClamp(&scene->ambient_colour, &scene->ambient_colour, 0.0f, 1.0f);

        if(scene->ambient_colour.v[0] == 1.0f && scene->ambient_colour.v[0] == 1.0f && scene->ambient_colour.v[0] == 1.0f)
            use_ambient_colour = BR_FALSE;
    }

    /* BRenderModern: No, sorry, I'm not dealing with lighting alpha. */
    scene->ambient_colour.v[3] = 1.0f;
    scene->use_ambient_colour  = use_ambient_colour;
}

/* BRenderModern:
** Update the per-model matrices.
**
** A good reference of the types is here:
** http://cse.csusb.edu/tongyu/courses/cs520/notes/glsl.php
*/
static void UpdateMatrices(state_cache *cache, state_matrix *matrix)
{
    if(matrix->view_to_environment_hint != BRT_DONT_CARE) {
        br_matrix34 tmp;
        BrMatrix34Mul(&tmp, &matrix->model_to_view, &matrix->view_to_environment);
        BrMatrix4Copy34(&cache->model.environment, &tmp);
    }

    /* BRenderModern:
     * Projection Matrix
     */
    BrMatrix4Copy(&cache->model.p, &matrix->view_to_screen);

    /* BRenderModern:
     * ModelView Matrix
     */
    BrMatrix4Copy34(&cache->model.mv, &matrix->model_to_view);
    cache->model.mv_det3 = BrMatrix34Determinant3(&matrix->model_to_view);

    /* BRenderModern:
     * Inverse of ModelView.
     */
    BrMatrix4Inverse(&cache->model.view_to_model, &cache->model.mv);

    /* BRenderModern:
     * MVP Matrix
     */
    BrMatrix4Mul(&cache->model.mvp, &cache->model.mv, &cache->model.p);

    /* BRenderModern:
     * Normal Matrix
     */
    BrMatrix4Inverse(&cache->model.normal, &cache->model.mv);
    BrMatrix4Transpose(&cache->model.normal);
}

/* BRenderModern:
 * Find centre of projection in model space
 */
static br_vector4 EyeInModel(const state_cache *cache, const state_matrix *matrix)
{
    br_matrix4 s2m;
    br_vector4 eye_m;

    /* BRenderModern:
     * Spot special, easy, cases
     */
    if(matrix->model_to_view_hint == BRT_LENGTH_PRESERVING) {
        if(matrix->view_to_screen_hint == BRT_PERSPECTIVE) {
            // BRenderModern: clang-format off
            eye_m.v[0] = -BR_MAC3(matrix->model_to_view.m[3][0], matrix->model_to_view.m[0][0],
                                  matrix->model_to_view.m[3][1], matrix->model_to_view.m[0][1],
                                  matrix->model_to_view.m[3][Z], matrix->model_to_view.m[0][2]);
            eye_m.v[1] = -BR_MAC3(matrix->model_to_view.m[3][0], matrix->model_to_view.m[1][0],
                                  matrix->model_to_view.m[3][1], matrix->model_to_view.m[1][1],
                                  matrix->model_to_view.m[3][Z], matrix->model_to_view.m[1][2]);
            eye_m.v[2] = -BR_MAC3(matrix->model_to_view.m[3][0], matrix->model_to_view.m[2][0],
                                  matrix->model_to_view.m[3][1], matrix->model_to_view.m[2][1],
                                  matrix->model_to_view.m[3][Z], matrix->model_to_view.m[2][2]);

            eye_m.v[3] = BR_SCALAR(1.0);
            // BRenderModern: clang-format on

            return eye_m;
        }

        if(matrix->view_to_screen_hint == BRT_PARALLEL) {
            BrVector3CopyMat34Col((br_vector3 *)&eye_m, &matrix->model_to_view, 2);
            eye_m.v[3] = BR_SCALAR(0.0);
            return eye_m;
        }

    } else {
        if(matrix->view_to_screen_hint == BRT_PERSPECTIVE) {
            BrVector3CopyMat34Row((br_vector3 *)&eye_m, &cache->model.view_to_model, 3);
            eye_m.v[3] = BR_SCALAR(1.0);
            return eye_m;
        }

        if(matrix->view_to_screen_hint == BRT_PARALLEL) {
            BrVector3CopyMat34Row((br_vector3 *)&eye_m, &cache->model.view_to_model, 2);
            eye_m.v[3] = BR_SCALAR(0.0);
            return eye_m;
        }
    }

    /* BRenderModern:
     * If reached here, then we need to invert model_to_screen
     */
    BrMatrix4Inverse(&s2m, &cache->model.mvp);

    eye_m.v[0] = s2m.m[Z][0];
    eye_m.v[1] = s2m.m[Z][1];
    eye_m.v[2] = s2m.m[Z][2];
    eye_m.v[3] = s2m.m[Z][3];
    return eye_m;
}

void StateGLUpdateModel(state_cache *cache, state_matrix *matrix)
{
    UpdateMatrices(cache, matrix);

    cache->model.eye_m = EyeInModel(cache, matrix);
}

void StateGLUpdateScene(state_cache *cache, state_stack *state)
{
    ASSERT(state->output.colour);
    cache->fbo = state->output.colour->asBack.glFbo;

    BrGLVector4FSet(&cache->scene.eye_view, 0.0f, 0.0f, 1.0f, 0.0f);

    /*
     * BRender's light cache is view-space. Supply fixed world axes transformed
     * into that same space so the shadow-map basis stays world-stable when the
     * main camera rotates.
     */
    {
        br_vector3 world_axis;
        br_vector3 view_axis;

        BrVector3Set(&world_axis, BR_SCALAR(0.0), BR_SCALAR(1.0), BR_SCALAR(0.0));
        BrMatrix34ApplyV(&view_axis, &world_axis, &state->matrix.model_to_view);
        BrGLVector4FSet(&cache->scene.shadow_world_up_view,
                        BrScalarToFloat(view_axis.v[0]), BrScalarToFloat(view_axis.v[1]),
                        BrScalarToFloat(view_axis.v[2]), 0.0f);

        BrVector3Set(&world_axis, BR_SCALAR(1.0), BR_SCALAR(0.0), BR_SCALAR(0.0));
        BrMatrix34ApplyV(&view_axis, &world_axis, &state->matrix.model_to_view);
        BrGLVector4FSet(&cache->scene.shadow_world_right_view,
                        BrScalarToFloat(view_axis.v[0]), BrScalarToFloat(view_axis.v[1]),
                        BrScalarToFloat(view_axis.v[2]), 0.0f);
    }

    ProcessSceneLights(cache, state->light);

    cache->scene.num_clip_planes = 0;
    if(state->valid & MASK_STATE_CLIP) {
        for(int i = 0; i < BR_ASIZE(cache->scene.clip_planes); ++i) {
            const state_clip *cp = state->clip + i;
            if(cp->type != BRT_PLANE)
                continue;

            /* BRenderModern:
             * BrSetupClipPlanes() does "Push plane through to screen space".
             * We need to undo that particular transformation.
             */
            br_vector4 plane;
            BrMatrix4TApply(&plane, &cp->plane, &state->matrix.view_to_screen);
            BrGLVector4ToFloat(cache->scene.clip_planes + i, &plane);

            ++cache->scene.num_clip_planes;
        }
    }
}

void StateGLReset(state_cache *cache)
{
    BrMatrix4Identity(&cache->model.p);
    BrMatrix4Identity(&cache->model.mv);
    BrMatrix4Identity(&cache->model.mvp);
    BrMatrix4Identity(&cache->model.normal);
    BrMatrix4Identity(&cache->model.environment);
    cache->model.mv_det3 = 0;
    BrVector4Set(&cache->model.eye_m, 0, 0, 0, 1);

    BrGLVector4FSet(&cache->scene.eye_view, 0.0f, 0.0f, 0.0f, 0.0f);

    BrMemSet(&cache->scene, 0, sizeof(cache->scene));
}
