/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: enables.c 1.7 1998/08/17 14:53:21 jon Exp $
 * $Locker: $
 *
 * Enabling/disabling of 'special' places in actor hierarchy
 */
#include "v1db.h"
#include "shortcut.h"
#include "brassert.h"
#include "math_ip.h"

#if BRENDER_LEGACY_3DMM_MODEL_ABI
static br_token Br4DMMShadowToken(void)
{
    static br_token token = BR_NULL_TOKEN;
    if(token == BR_NULL_TOKEN)
        token = BrTokenCreate("SHADOW_B", BRT_BOOLEAN);
    return token;
}

/*
 * v218: pass the exact view->light transform already calculated by
 * BrSetupLights() into glrend.  The shadow projection can then combine this
 * with each model->view transform on the CPU, eliminating the main camera from
 * the final model->light matrix instead of reconstructing a light basis from
 * camera-space position/direction vectors in GLSL.
 */
static br_token Br4DMMShadowViewToLightToken(void)
{
    static br_token token = BR_NULL_TOKEN;
    if(token == BR_NULL_TOKEN) {
#if BASED_FIXED
        token = BrTokenCreate("SHADOW_VIEW_TO_LIGHT_M34_X", BRT_MATRIX34_FIXED);
#else
        token = BrTokenCreate("SHADOW_VIEW_TO_LIGHT_M34_F", BRT_MATRIX34_FLOAT);
#endif
    }
    return token;
}

static br_token Br4DMMShadowFitToken(void)
{
    static br_token token = BR_NULL_TOKEN;
    if(token == BR_NULL_TOKEN)
        token = BrTokenCreate("SHADOW_FIT_V4_F", BRT_VECTOR4_FLOAT);
    return token;
}

/*
 * v256: the physical detail map follows shadow rays that can enter the camera
 * frustum, not merely geometry whose own vertices are visible.  A caster can
 * therefore walk completely off-screen while its long projected shadow remains
 * in view; the same light->caster ray stays in the high-resolution crop until
 * that ray itself no longer intersects the camera view volume.
 */
static br_token Br4DMMShadowDetailFitToken(void)
{
    static br_token token = BR_NULL_TOKEN;
    if(token == BR_NULL_TOKEN)
        token = BrTokenCreate("SHADOW_DETAIL_FIT_V4_F", BRT_VECTOR4_FLOAT);
    return token;
}

typedef struct br_4dmm_shadow_fit_accum {
    br_float min_u;
    br_float max_u;
    br_float min_v;
    br_float max_v;
    br_uint_32 point_count;
    br_uint_32 model_count;
} br_4dmm_shadow_fit_accum;

/*
 * v260: the physical detail texture is still one 15360-ish map, but fitting it
 * around every relevant caster at once dilutes its angular density in crowded
 * scenes.  Track the same logical-object key used by the owner map so the
 * detail map can spend its full resolution on the visible shadow that is most
 * resolution-starved.  The base 8192 map remains authoritative coverage for
 * every other caster.
 */
#define BR_4DMM_SHADOW_DETAIL_OWNER_MAX 254u
typedef struct br_4dmm_shadow_detail_owner {
    const void *key;
    /* Full casting geometry for this logical object. Camera visibility must
     * select an owner, not clip the owner's detail coverage. */
    br_4dmm_shadow_fit_accum fit;
    br_uint_32 visible_point_count;
    br_uint_32 visible_model_count;
    br_float max_visible_t;
    br_float min_center_metric;
} br_4dmm_shadow_detail_owner;

typedef struct br_4dmm_shadow_detail_owners {
    br_4dmm_shadow_detail_owner owner[BR_4DMM_SHADOW_DETAIL_OWNER_MAX];
    br_uint_32 count;
    br_boolean overflow_logged;
} br_4dmm_shadow_detail_owners;

static const void *Br4DMMShadowDetailOwnerKey(const br_actor *actor)
{
    if(actor == NULL)
        return NULL;

    /* Match render.c's proven BODY-part owner grouping exactly. */
    return actor->identifier != NULL ? (const void *)actor->identifier : (const void *)actor;
}

static br_4dmm_shadow_detail_owner *Br4DMMShadowDetailOwnerFindOrAdd(br_4dmm_shadow_detail_owners *owners,
                                                                      const br_actor *actor)
{
    const void *key;
    br_uint_32 i;

    if(owners == NULL || actor == NULL)
        return NULL;

    key = Br4DMMShadowDetailOwnerKey(actor);
    for(i = 0; i < owners->count; ++i) {
        if(owners->owner[i].key == key)
            return &owners->owner[i];
    }

    if(owners->count >= BR_4DMM_SHADOW_DETAIL_OWNER_MAX) {
        if(!owners->overflow_logged) {
            BrWarning("SHADOW detail_owner_v260 overflow max=%u; falling back to union fit for extra owners",
                      (unsigned)BR_4DMM_SHADOW_DETAIL_OWNER_MAX);
            owners->overflow_logged = BR_TRUE;
        }
        return NULL;
    }

    owners->owner[owners->count].key = key;
    owners->owner[owners->count].max_visible_t = 1.0f;
    owners->owner[owners->count].min_center_metric = 1000000.0f;
    ++owners->count;
    return &owners->owner[owners->count - 1];
}

#define BR_4DMM_SHADOW_NO_CAST_SYSTEM 0x00000001u
#define BR_4DMM_SHADOW_NO_CAST_OBJECT 0x00000002u

static br_uint_32 Br4DMMShadowFitBodyFlags(const br_actor *actor)
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

static br_boolean Br4DMMShadowFitExcludedRoot(const br_actor *actor)
{
    if(actor == NULL || actor->type != BR_ACTOR_NONE)
        return BR_FALSE;

    /*
     * v233: user no-cast objects still occupy the shadow-stop depth layer and
     * therefore need coverage from the same fitted light projection. System
     * Light Object handles remain completely absent from shadow rendering.
     */
    return (Br4DMMShadowFitBodyFlags(actor) & BR_4DMM_SHADOW_NO_CAST_SYSTEM) != 0;
}

static void Br4DMMShadowFitAccumulateSlope(br_4dmm_shadow_fit_accum *fit, br_float u, br_float v)
{
    if(fit == NULL)
        return;

    if(fit->point_count == 0) {
        fit->min_u = fit->max_u = u;
        fit->min_v = fit->max_v = v;
    } else {
        if(u < fit->min_u) fit->min_u = u;
        if(u > fit->max_u) fit->max_u = u;
        if(v < fit->min_v) fit->min_v = v;
        if(v > fit->max_v) fit->max_v = v;
    }
    ++fit->point_count;
}

static br_boolean Br4DMMShadowPointVisibleToCamera(const br_vector3 *view_p, const br_camera *camera)
{
    const br_float margin = 1.12f;
    br_float x, y, z;
    br_float hither, yon;

    if(view_p == NULL || camera == NULL)
        return BR_FALSE;

    x = BrScalarToFloat(view_p->v[0]);
    y = BrScalarToFloat(view_p->v[1]);
    z = -BrScalarToFloat(view_p->v[2]);
    if(z <= 0.01f)
        return BR_FALSE;

    hither = BrScalarToFloat(camera->hither_z);
    yon = BrScalarToFloat(camera->yon_z);
    if(hither > 0.0f && z < hither)
        return BR_FALSE;
    if(yon > hither && z > yon)
        return BR_FALSE;

    switch(camera->type) {
        case BR_CAMERA_PERSPECTIVE_FOV:
        case BR_CAMERA_PERSPECTIVE_FOV_OLD: {
            br_float tan_y = BrScalarToFloat(BR_TAN((br_angle)(camera->field_of_view / 2)));
            br_float aspect = BrScalarToFloat(camera->aspect);
            br_float sx, sy;

            if(tan_y <= 0.0001f)
                return BR_FALSE;
            if(aspect <= 0.0001f)
                aspect = 1.0f;

            sx = x / z;
            sy = y / z;
            return sx >= -tan_y * aspect * margin && sx <= tan_y * aspect * margin &&
                   sy >= -tan_y * margin && sy <= tan_y * margin;
        }

        case BR_CAMERA_PERSPECTIVE_WHD: {
            br_float distance = BrScalarToFloat(camera->distance);
            br_float half_x, half_y;
            br_float sx, sy;

            if(distance <= 0.0001f)
                return BR_FALSE;

            half_x = BrScalarToFloat(camera->width) * 0.5f / distance;
            half_y = BrScalarToFloat(camera->height) * 0.5f / distance;
            if(half_x < 0.0f) half_x = -half_x;
            if(half_y < 0.0f) half_y = -half_y;

            sx = x / z;
            sy = y / z;
            return sx >= -half_x * margin && sx <= half_x * margin &&
                   sy >= -half_y * margin && sy <= half_y * margin;
        }

        case BR_CAMERA_PARALLEL:
        case BR_CAMERA_PARALLEL_OLD: {
            br_float half_x = BrScalarToFloat(camera->width) * 0.5f;
            br_float half_y = BrScalarToFloat(camera->height) * 0.5f;
            if(half_x < 0.0f) half_x = -half_x;
            if(half_y < 0.0f) half_y = -half_y;

            return x >= -half_x * margin && x <= half_x * margin &&
                   y >= -half_y * margin && y <= half_y * margin;
        }

        default:
            return BR_FALSE;
    }
}

static br_boolean Br4DMMShadowAccumulateLightPoint(br_4dmm_shadow_fit_accum *fit, const br_vector3 *light_p)
{
    br_float x, y, z;

    if(fit == NULL || light_p == NULL)
        return BR_FALSE;

    x = BrScalarToFloat(light_p->v[0]);
    y = BrScalarToFloat(light_p->v[1]);
    z = -BrScalarToFloat(light_p->v[2]);
    if(z <= 0.01f)
        return BR_FALSE;

    Br4DMMShadowFitAccumulateSlope(fit, x / z, y / z);
    return BR_TRUE;
}

static br_boolean Br4DMMShadowClipRayHalfspace(br_float a, br_float b, br_float *t_min, br_float *t_max)
{
    const br_float epsilon = 0.000001f;
    br_float t;

    if(b > -epsilon && b < epsilon)
        return a >= 0.0f;

    t = -a / b;
    if(b > 0.0f) {
        if(t > *t_min)
            *t_min = t;
    } else {
        if(t < *t_max)
            *t_max = t;
    }

    return *t_min <= *t_max;
}

/*
 * Test the physically relevant half-ray for a cast shadow: light -> caster ->
 * points beyond the caster.  All camera-frustum planes are linear in t, so a
 * small Liang-Barsky style interval clip tells us whether any potential shadow
 * point on that ray can still be visible even when the caster itself is not.
 */
static br_boolean Br4DMMShadowRayCameraInterval(const br_vector3 *light_view,
                                                const br_vector3 *caster_view,
                                                const br_camera *camera,
                                                br_float *out_t_min, br_float *out_t_max)
{
    const br_float margin = 1.12f;
    br_float lx, ly, lz, dx, dy, dz;
    br_float hither, yon;
    br_float t_min = 1.0f;
    br_float t_max = 1000000.0f;
    br_float half_x, half_y;

    if(light_view == NULL || caster_view == NULL || camera == NULL)
        return BR_FALSE;

    lx = BrScalarToFloat(light_view->v[0]);
    ly = BrScalarToFloat(light_view->v[1]);
    lz = -BrScalarToFloat(light_view->v[2]);
    dx = BrScalarToFloat(caster_view->v[0]) - lx;
    dy = BrScalarToFloat(caster_view->v[1]) - ly;
    dz = -BrScalarToFloat(caster_view->v[2]) - lz;

    hither = BrScalarToFloat(camera->hither_z);
    yon = BrScalarToFloat(camera->yon_z);
    if(hither < 0.0f)
        hither = 0.0f;

    if(!Br4DMMShadowClipRayHalfspace(lz - hither, dz, &t_min, &t_max))
        return BR_FALSE;
    if(yon > hither && !Br4DMMShadowClipRayHalfspace(yon - lz, -dz, &t_min, &t_max))
        return BR_FALSE;

    switch(camera->type) {
        case BR_CAMERA_PERSPECTIVE_FOV:
        case BR_CAMERA_PERSPECTIVE_FOV_OLD:
            half_y = BrScalarToFloat(BR_TAN((br_angle)(camera->field_of_view / 2)));
            half_x = half_y * BrScalarToFloat(camera->aspect);
            if(half_y <= 0.0001f)
                return BR_FALSE;
            if(half_x <= 0.0001f)
                half_x = half_y;
            half_x *= margin;
            half_y *= margin;

            if(!Br4DMMShadowClipRayHalfspace(half_x * lz - lx, half_x * dz - dx, &t_min, &t_max) ||
               !Br4DMMShadowClipRayHalfspace(half_x * lz + lx, half_x * dz + dx, &t_min, &t_max) ||
               !Br4DMMShadowClipRayHalfspace(half_y * lz - ly, half_y * dz - dy, &t_min, &t_max) ||
               !Br4DMMShadowClipRayHalfspace(half_y * lz + ly, half_y * dz + dy, &t_min, &t_max))
                return BR_FALSE;
            break;

        case BR_CAMERA_PERSPECTIVE_WHD: {
            br_float distance = BrScalarToFloat(camera->distance);
            if(distance <= 0.0001f)
                return BR_FALSE;
            half_x = BrScalarToFloat(camera->width) * 0.5f / distance;
            half_y = BrScalarToFloat(camera->height) * 0.5f / distance;
            if(half_x < 0.0f) half_x = -half_x;
            if(half_y < 0.0f) half_y = -half_y;
            half_x *= margin;
            half_y *= margin;

            if(!Br4DMMShadowClipRayHalfspace(half_x * lz - lx, half_x * dz - dx, &t_min, &t_max) ||
               !Br4DMMShadowClipRayHalfspace(half_x * lz + lx, half_x * dz + dx, &t_min, &t_max) ||
               !Br4DMMShadowClipRayHalfspace(half_y * lz - ly, half_y * dz - dy, &t_min, &t_max) ||
               !Br4DMMShadowClipRayHalfspace(half_y * lz + ly, half_y * dz + dy, &t_min, &t_max))
                return BR_FALSE;
            break;
        }

        case BR_CAMERA_PARALLEL:
        case BR_CAMERA_PARALLEL_OLD:
            half_x = BrScalarToFloat(camera->width) * 0.5f;
            half_y = BrScalarToFloat(camera->height) * 0.5f;
            if(half_x < 0.0f) half_x = -half_x;
            if(half_y < 0.0f) half_y = -half_y;
            half_x *= margin;
            half_y *= margin;

            if(!Br4DMMShadowClipRayHalfspace(half_x - lx, -dx, &t_min, &t_max) ||
               !Br4DMMShadowClipRayHalfspace(half_x + lx, dx, &t_min, &t_max) ||
               !Br4DMMShadowClipRayHalfspace(half_y - ly, -dy, &t_min, &t_max) ||
               !Br4DMMShadowClipRayHalfspace(half_y + ly, dy, &t_min, &t_max))
                return BR_FALSE;
            break;

        default:
            return BR_FALSE;
    }

    if(t_min > t_max)
        return BR_FALSE;

    if(out_t_min != NULL)
        *out_t_min = t_min;
    if(out_t_max != NULL)
        *out_t_max = t_max;
    return BR_TRUE;
}

/*
 * Return squared normalized screen distance from the camera centre for one
 * point expressed as x/y and positive-forward z in camera space.
 */
static br_float Br4DMMShadowCameraCenterMetric(br_float x, br_float y, br_float z, const br_camera *camera)
{
    br_float nx, ny, half_x, half_y;

    if(camera == NULL || z <= 0.0001f)
        return 1000000.0f;

    switch(camera->type) {
        case BR_CAMERA_PERSPECTIVE_FOV:
        case BR_CAMERA_PERSPECTIVE_FOV_OLD:
            half_y = BrScalarToFloat(BR_TAN((br_angle)(camera->field_of_view / 2)));
            half_x = half_y * BrScalarToFloat(camera->aspect);
            if(half_y <= 0.0001f)
                return 1000000.0f;
            if(half_x <= 0.0001f)
                half_x = half_y;
            nx = x / (z * half_x);
            ny = y / (z * half_y);
            return nx * nx + ny * ny;

        case BR_CAMERA_PERSPECTIVE_WHD:
            if(BrScalarToFloat(camera->distance) <= 0.0001f)
                return 1000000.0f;
            half_x = BrScalarToFloat(camera->width) * 0.5f / BrScalarToFloat(camera->distance);
            half_y = BrScalarToFloat(camera->height) * 0.5f / BrScalarToFloat(camera->distance);
            if(half_x < 0.0f) half_x = -half_x;
            if(half_y < 0.0f) half_y = -half_y;
            if(half_x <= 0.0001f || half_y <= 0.0001f)
                return 1000000.0f;
            nx = x / (z * half_x);
            ny = y / (z * half_y);
            return nx * nx + ny * ny;

        case BR_CAMERA_PARALLEL:
        case BR_CAMERA_PARALLEL_OLD:
            half_x = BrScalarToFloat(camera->width) * 0.5f;
            half_y = BrScalarToFloat(camera->height) * 0.5f;
            if(half_x < 0.0f) half_x = -half_x;
            if(half_y < 0.0f) half_y = -half_y;
            if(half_x <= 0.0001f || half_y <= 0.0001f)
                return 1000000.0f;
            nx = x / half_x;
            ny = y / half_y;
            return nx * nx + ny * ny;

        default:
            return 1000000.0f;
    }
}

/*
 * Approximate the closest approach of the visible light->caster half-ray to
 * screen centre. Entry/middle/exit plus x/y axis crossings are enough to keep
 * the detail focus attached to the shadow the user is actually looking at.
 */
static br_float Br4DMMShadowRayCenterMetric(const br_vector3 *light_view,
                                             const br_vector3 *caster_view,
                                             const br_camera *camera,
                                             br_float t_min, br_float t_max)
{
    br_float lx, ly, lz, dx, dy, dz;
    br_float tc[5];
    br_float best = 1000000.0f;
    int i;

    if(light_view == NULL || caster_view == NULL || camera == NULL || t_min > t_max)
        return best;

    lx = BrScalarToFloat(light_view->v[0]);
    ly = BrScalarToFloat(light_view->v[1]);
    lz = -BrScalarToFloat(light_view->v[2]);
    dx = BrScalarToFloat(caster_view->v[0]) - lx;
    dy = BrScalarToFloat(caster_view->v[1]) - ly;
    dz = -BrScalarToFloat(caster_view->v[2]) - lz;

    tc[0] = t_min;
    tc[1] = (t_min + t_max) * 0.5f;
    tc[2] = t_max;
    tc[3] = (dx > 0.000001f || dx < -0.000001f) ? -lx / dx : t_min;
    tc[4] = (dy > 0.000001f || dy < -0.000001f) ? -ly / dy : t_min;

    for(i = 0; i < 5; ++i) {
        br_float t = tc[i];
        br_float metric;
        if(t < t_min) t = t_min;
        if(t > t_max) t = t_max;
        metric = Br4DMMShadowCameraCenterMetric(lx + dx * t, ly + dy * t, lz + dz * t, camera);
        if(metric < best)
            best = metric;
    }

    return best;
}

static void Br4DMMShadowFitAccumulateModel(br_4dmm_shadow_fit_accum *fit,
                                           br_4dmm_shadow_fit_accum *detail_fit,
                                           br_4dmm_shadow_detail_owners *detail_owners,
                                           br_actor *actor, br_actor *camera,
                                           br_actor *light_actor, const br_model *model)
{
    br_matrix34 model_to_light;
    br_matrix34 model_to_view;
    br_matrix34 light_to_view;
    br_vector3 sample;
    br_vector3 light_p;
    br_vector3 view_p;
    br_vector3 light_view;
    br_vector3 origin = {{0, 0, 0}};
    br_boolean detail_candidate;
    br_boolean detail_model_seen = BR_FALSE;
    br_boolean detail_owner_full_model_seen = BR_FALSE;
    br_boolean detail_owner_visible_model_seen = BR_FALSE;
    br_4dmm_shadow_detail_owner *detail_owner = NULL;
    br_float ray_t_min = 1.0f;
    br_float ray_t_max = 1.0f;
    br_float ray_center_metric = 1000000.0f;
    br_uint_32 iv;
    int ix, iy, iz;

    if(fit == NULL || actor == NULL || light_actor == NULL || model == NULL)
        return;

    BrActorToActorMatrix34(&model_to_light, actor, light_actor);

    /*
     * Keep the proven v228 base fit conservative: all eight model AABB corners
     * participate whether or not they are visible to the main camera.
     */
    for(ix = 0; ix < 2; ++ix) {
        for(iy = 0; iy < 2; ++iy) {
            for(iz = 0; iz < 2; ++iz) {
                sample.v[0] = ix ? model->bounds.max.v[0] : model->bounds.min.v[0];
                sample.v[1] = iy ? model->bounds.max.v[1] : model->bounds.min.v[1];
                sample.v[2] = iz ? model->bounds.max.v[2] : model->bounds.min.v[2];

                BrMatrix34ApplyP(&light_p, &sample, &model_to_light);
                (void)Br4DMMShadowAccumulateLightPoint(fit, &light_p);
            }
        }
    }

    /*
     * v256 detail fit: a shadow remains relevant whenever the half-ray from
     * the light through a casting point can enter the camera frustum at or
     * beyond that point.  This is the shadow's actual geometry, so it does not
     * matter whether the caster itself is on-screen.  In particular, an actor
     * can walk out of frame while a long sunset-style shadow stays visible and
     * the detail crop will continue to follow the moving actor's light-space
     * slopes instead of falling back to the coarse base map.
     *
     * Only real shadow casters drive this crop.  User shadow-casting=0 objects
     * still receive/block shadows through the established base owner/blocker
     * maps, but do not waste the one high-resolution depth cascade.
     */
    detail_candidate = detail_fit != NULL && camera != NULL &&
                       (Br4DMMShadowFitBodyFlags(actor) &
                        (BR_4DMM_SHADOW_NO_CAST_SYSTEM | BR_4DMM_SHADOW_NO_CAST_OBJECT)) == 0;

    if(detail_candidate) {
        BrActorToActorMatrix34(&model_to_view, actor, camera);
        BrActorToActorMatrix34(&light_to_view, light_actor, camera);
        BrMatrix34ApplyP(&light_view, &origin, &light_to_view);

        /*
         * v262 separates relevance from coverage. v256-v261 used only the
         * subset of caster vertices whose light rays intersected the camera
         * frustum to build each owner's detail fit. A tiny camera pitch could
         * therefore add/remove BODY vertices from the crop and make one
         * logical object's shadow switch between base/detail representations.
         *
         * Camera-ray intersection still decides whether this owner is relevant
         * and supplies its ranking metrics, but once an owner is relevant its
         * fit below is built from ALL of that owner's casting geometry.
         */
        if(detail_owners != NULL)
            detail_owner = Br4DMMShadowDetailOwnerFindOrAdd(detail_owners, actor);

        if(model->vertices != NULL && model->nvertices > 0) {
            for(iv = 0; iv < model->nvertices; ++iv) {
                sample = model->vertices[iv].p;
                BrMatrix34ApplyP(&light_p, &sample, &model_to_light);
                if(detail_owner != NULL && Br4DMMShadowAccumulateLightPoint(&detail_owner->fit, &light_p))
                    detail_owner_full_model_seen = BR_TRUE;

                BrMatrix34ApplyP(&view_p, &sample, &model_to_view);
                ray_t_min = ray_t_max = 1.0f;
                if(!Br4DMMShadowPointVisibleToCamera(&view_p, (const br_camera *)camera->type_data) &&
                   !Br4DMMShadowRayCameraInterval(&light_view, &view_p, (const br_camera *)camera->type_data,
                                                   &ray_t_min, &ray_t_max))
                    continue;

                /* Obtain the interval even for an on-screen caster so v260 can
                 * rank how far its shadow ray remains visible. */
                if(ray_t_max <= 1.0f)
                    (void)Br4DMMShadowRayCameraInterval(&light_view, &view_p,
                                                        (const br_camera *)camera->type_data,
                                                        &ray_t_min, &ray_t_max);

                if(Br4DMMShadowAccumulateLightPoint(detail_fit, &light_p)) {
                    detail_model_seen = BR_TRUE;
                    if(detail_owner != NULL) {
                        ++detail_owner->visible_point_count;
                        detail_owner_visible_model_seen = BR_TRUE;
                        if(ray_t_max > detail_owner->max_visible_t)
                            detail_owner->max_visible_t = ray_t_max;
                        ray_center_metric = Br4DMMShadowRayCenterMetric(&light_view, &view_p,
                                                                        (const br_camera *)camera->type_data,
                                                                        ray_t_min, ray_t_max);
                        if(ray_center_metric < detail_owner->min_center_metric)
                            detail_owner->min_center_metric = ray_center_metric;
                    }
                }
            }
        } else {
            /* Legacy/prepared fallback: use all eight bounds corners for the
             * owner's full fit and test those same rays for relevance. */
            for(ix = 0; ix < 2; ++ix) {
                for(iy = 0; iy < 2; ++iy) {
                    for(iz = 0; iz < 2; ++iz) {
                        sample.v[0] = ix ? model->bounds.max.v[0] : model->bounds.min.v[0];
                        sample.v[1] = iy ? model->bounds.max.v[1] : model->bounds.min.v[1];
                        sample.v[2] = iz ? model->bounds.max.v[2] : model->bounds.min.v[2];
                        BrMatrix34ApplyP(&light_p, &sample, &model_to_light);
                        if(detail_owner != NULL && Br4DMMShadowAccumulateLightPoint(&detail_owner->fit, &light_p))
                            detail_owner_full_model_seen = BR_TRUE;

                        BrMatrix34ApplyP(&view_p, &sample, &model_to_view);
                        ray_t_min = ray_t_max = 1.0f;
                        if(!Br4DMMShadowPointVisibleToCamera(&view_p, (const br_camera *)camera->type_data) &&
                           !Br4DMMShadowRayCameraInterval(&light_view, &view_p, (const br_camera *)camera->type_data,
                                                           &ray_t_min, &ray_t_max))
                            continue;
                        if(ray_t_max <= 1.0f)
                            (void)Br4DMMShadowRayCameraInterval(&light_view, &view_p,
                                                                (const br_camera *)camera->type_data,
                                                                &ray_t_min, &ray_t_max);

                        if(Br4DMMShadowAccumulateLightPoint(detail_fit, &light_p)) {
                            detail_model_seen = BR_TRUE;
                            if(detail_owner != NULL) {
                                ++detail_owner->visible_point_count;
                                detail_owner_visible_model_seen = BR_TRUE;
                                if(ray_t_max > detail_owner->max_visible_t)
                                    detail_owner->max_visible_t = ray_t_max;
                                ray_center_metric = Br4DMMShadowRayCenterMetric(&light_view, &view_p,
                                                                                (const br_camera *)camera->type_data,
                                                                                ray_t_min, ray_t_max);
                                if(ray_center_metric < detail_owner->min_center_metric)
                                    detail_owner->min_center_metric = ray_center_metric;
                            }
                        }
                    }
                }
            }
        }

        /*
         * Centre fallback for coarse/degenerate meshes.  This also gives thin
         * legacy BODY pieces a stable representative ray if their stored
         * vertices happen to miss the frustum while their silhouette does not.
         */
        if(!detail_model_seen) {
            sample.v[0] = (model->bounds.min.v[0] + model->bounds.max.v[0]) / 2;
            sample.v[1] = (model->bounds.min.v[1] + model->bounds.max.v[1]) / 2;
            sample.v[2] = (model->bounds.min.v[2] + model->bounds.max.v[2]) / 2;
            BrMatrix34ApplyP(&view_p, &sample, &model_to_view);
            ray_t_min = ray_t_max = 1.0f;
            if(Br4DMMShadowPointVisibleToCamera(&view_p, (const br_camera *)camera->type_data) ||
               Br4DMMShadowRayCameraInterval(&light_view, &view_p, (const br_camera *)camera->type_data,
                                               &ray_t_min, &ray_t_max)) {
                if(ray_t_max <= 1.0f)
                    (void)Br4DMMShadowRayCameraInterval(&light_view, &view_p,
                                                        (const br_camera *)camera->type_data,
                                                        &ray_t_min, &ray_t_max);
                BrMatrix34ApplyP(&light_p, &sample, &model_to_light);
                if(Br4DMMShadowAccumulateLightPoint(detail_fit, &light_p)) {
                    detail_model_seen = BR_TRUE;
                    if(detail_owner != NULL) {
                        ++detail_owner->visible_point_count;
                        detail_owner_visible_model_seen = BR_TRUE;
                        if(ray_t_max > detail_owner->max_visible_t)
                            detail_owner->max_visible_t = ray_t_max;
                        ray_center_metric = Br4DMMShadowRayCenterMetric(&light_view, &view_p,
                                                                        (const br_camera *)camera->type_data,
                                                                        ray_t_min, ray_t_max);
                        if(ray_center_metric < detail_owner->min_center_metric)
                            detail_owner->min_center_metric = ray_center_metric;
                    }
                }
            }
        }
    }

    ++fit->model_count;
    if(detail_model_seen)
        ++detail_fit->model_count;
    if(detail_owner != NULL) {
        if(detail_owner_full_model_seen)
            ++detail_owner->fit.model_count;
        if(detail_owner_visible_model_seen)
            ++detail_owner->visible_model_count;
    }
}

static void Br4DMMShadowFitAccumulateTree(br_4dmm_shadow_fit_accum *fit,
                                          br_4dmm_shadow_fit_accum *detail_fit,
                                          br_4dmm_shadow_detail_owners *detail_owners,
                                          br_actor *actor, br_actor *camera,
                                          br_actor *light_actor, br_uint_8 inherited_style)
{
    br_uint_8 style;
    br_actor *child;

    if(actor == NULL || Br4DMMShadowFitExcludedRoot(actor))
        return;

    style = actor->render_style != BR_RSTYLE_DEFAULT ? actor->render_style : inherited_style;

    if(actor->type == BR_ACTOR_MODEL && actor->model != NULL && style != BR_RSTYLE_BOUNDING_EDGES)
        Br4DMMShadowFitAccumulateModel(fit, detail_fit, detail_owners, actor, camera, light_actor, actor->model);

    BR_FOR_SIMPLELIST(&actor->children, child)
        Br4DMMShadowFitAccumulateTree(fit, detail_fit, detail_owners, child, camera, light_actor, style);
}

/*
 * v228 fitted shadow projection. The physical shadow texture stays at whatever
 * size the GL driver can reliably allocate, but its angular coverage is cropped
 * to the actual caster population. A 32x maximum crop lets an 8192 map provide
 * up to ~262K-equivalent angular sampling for compact scenes without allocating
 * a 262K texture.
 *
 * v256 drives the existing 15360-ish detail texture from casting points whose
 * light->caster shadow half-rays intersect the camera frustum.  The crop can
 * therefore follow an off-screen moving actor for as long as its projected ray
 * can still produce a visible shadow.
 *
 * v258 removes the old 16x detail-crop floor as the practical resolution
 * ceiling for distant casters.
 *
 * v260 introduced logical-owner focus for the one physical detail map.
 *
 * v261 fixes the focus criterion exposed by the close-up long-shadow test. A
 * large receiver/floor owner could win merely because one of its own casting
 * rays passed through screen centre, even though fitting that owner yielded
 * almost no angular magnification. A small camera pitch could then switch to
 * the actual BODY owner and jump from roughly 2x to roughly 70x detail.
 *
 * Prefer owners by useful angular density first, then camera relevance and ray
 * stretch. Large floor-like owners therefore stop stealing the detail cascade
 * from compact actor/prop casters whose visible shadows actually benefit from
 * it. The base shadow map still covers every caster.
 */
static br_boolean Br4DMMShadowComputeFit(br_actor *world, br_actor *camera,
                                         br_actor *light_actor, br_float tan_half,
                                         br_vector4_f *out_fit, br_vector4_f *out_detail_fit)
{
    br_4dmm_shadow_fit_accum fit = {0};
    br_4dmm_shadow_fit_accum detail_fit = {0};
    br_4dmm_shadow_detail_owners detail_owners = {0};
    br_actor *child;
    br_float center_u, center_v, half_u, half_v;
    br_float min_half;
    const br_float padding = 1.08f;
    const br_float max_zoom = 32.0f;

    if(world == NULL || camera == NULL || camera->type_data == NULL ||
       light_actor == NULL || out_fit == NULL || tan_half <= 0.0001f)
        return BR_FALSE;

    BR_FOR_SIMPLELIST(&world->children, child)
        Br4DMMShadowFitAccumulateTree(&fit, out_detail_fit != NULL ? &detail_fit : NULL,
                                      out_detail_fit != NULL ? &detail_owners : NULL,
                                      child, camera, light_actor, BR_RSTYLE_DEFAULT);

    if(fit.point_count == 0)
        return BR_FALSE;

    if(fit.min_u < -tan_half) fit.min_u = -tan_half;
    if(fit.max_u >  tan_half) fit.max_u =  tan_half;
    if(fit.min_v < -tan_half) fit.min_v = -tan_half;
    if(fit.max_v >  tan_half) fit.max_v =  tan_half;
    if(fit.min_u >= fit.max_u || fit.min_v >= fit.max_v)
        return BR_FALSE;

    center_u = (fit.min_u + fit.max_u) * 0.5f;
    center_v = (fit.min_v + fit.max_v) * 0.5f;
    half_u = (fit.max_u - fit.min_u) * 0.5f * padding;
    half_v = (fit.max_v - fit.min_v) * 0.5f * padding;

    min_half = tan_half / max_zoom;
    if(half_u < min_half) half_u = min_half;
    if(half_v < min_half) half_v = min_half;
    if(half_u > tan_half) half_u = tan_half;
    if(half_v > tan_half) half_v = tan_half;

    /* Keep the padded crop inside the physical spotlight cone. */
    if(center_u - half_u < -tan_half) center_u = -tan_half + half_u;
    if(center_u + half_u >  tan_half) center_u =  tan_half - half_u;
    if(center_v - half_v < -tan_half) center_v = -tan_half + half_v;
    if(center_v + half_v >  tan_half) center_v =  tan_half - half_v;

    out_fit->v[0] = center_u;
    out_fit->v[1] = center_v;
    out_fit->v[2] = half_u;
    out_fit->v[3] = half_v;

    if(out_detail_fit != NULL && detail_fit.point_count > 0) {
        static const void *previous_detail_owner_key = NULL;
        const br_4dmm_shadow_fit_accum *detail_source = &detail_fit;
        const br_4dmm_shadow_detail_owner *selected_owner = NULL;
        const br_4dmm_shadow_detail_owner *previous_owner = NULL;
        br_float best_score = 0.0f;
        br_float previous_score = 0.0f;
        br_float best_useful_zoom = 1.0f;
        br_float previous_useful_zoom = 1.0f;
        br_uint_32 io;
        br_boolean kept_previous = BR_FALSE;
        const br_float detail_padding = 1.20f;
        const br_float detail_max_zoom = 64.0f;
        const br_float base_min_u = center_u - half_u;
        const br_float base_max_u = center_u + half_u;
        const br_float base_min_v = center_v - half_v;
        const br_float base_max_v = center_v + half_v;
        br_float dmin_u, dmax_u, dmin_v, dmax_v;
        br_float dcenter_u, dcenter_v, dhalf_u, dhalf_v;

        /*
         * v261: focus the physical detail map where it can buy real angular
         * resolution.  The v260 screen-centre score allowed a huge floor-like
         * owner to win with an almost base-sized crop, then abruptly switch to
         * the compact BODY owner after a tiny camera pitch.  That was the
         * stair-step -> wavy transition seen in the 260 close-ups.
         *
         * useful_zoom estimates the minimum 2-D magnification this owner's own
         * padded fit can achieve inside the current base fit.  Make that the
         * dominant term. Camera-centre distance only biases between similarly
         * useful owners, and long visible rays get a small secondary bonus.
         *
         * v262 separates owner selection from owner coverage: the full logical
         * caster bounds define the crop while camera-visible rays only decide
         * relevance. This prevents camera pitch from clipping BODY pieces out
         * of the detail map and stitching one shadow from base/detail regions.
         */
        for(io = 0; io < detail_owners.count; ++io) {
            const br_4dmm_shadow_detail_owner *owner = &detail_owners.owner[io];
            br_float center_metric = owner->min_center_metric;
            br_float stretch = owner->max_visible_t;
            br_float owner_half_u, owner_half_v;
            br_float useful_zoom_u, useful_zoom_v, useful_zoom;
            br_float relevance_divisor, stretch_bonus;
            br_float score;

            /* The fit is full-owner coverage in v262. Visibility is a
             * separate eligibility/ranking signal. */
            if(owner->fit.point_count == 0 || owner->visible_point_count == 0)
                continue;

            owner_half_u = (owner->fit.max_u - owner->fit.min_u) * 0.5f * detail_padding;
            owner_half_v = (owner->fit.max_v - owner->fit.min_v) * 0.5f * detail_padding;
            if(owner_half_u < half_u / detail_max_zoom) owner_half_u = half_u / detail_max_zoom;
            if(owner_half_v < half_v / detail_max_zoom) owner_half_v = half_v / detail_max_zoom;
            if(owner_half_u > half_u) owner_half_u = half_u;
            if(owner_half_v > half_v) owner_half_v = half_v;

            useful_zoom_u = owner_half_u > 0.0f ? half_u / owner_half_u : 1.0f;
            useful_zoom_v = owner_half_v > 0.0f ? half_v / owner_half_v : 1.0f;
            useful_zoom = useful_zoom_u < useful_zoom_v ? useful_zoom_u : useful_zoom_v;
            if(useful_zoom < 1.0f) useful_zoom = 1.0f;
            if(useful_zoom > detail_max_zoom) useful_zoom = detail_max_zoom;

            if(center_metric < 0.0f) center_metric = 0.0f;
            if(center_metric > 16.0f) center_metric = 16.0f;
            if(stretch < 1.0f) stretch = 1.0f;
            if(stretch > 32.0f) stretch = 32.0f;

            /*
             * v262: useful_zoom is now an eligibility check rather than the
             * dominant winner-take-all term. v261's 64x-first score naturally
             * preferred tiny unrelated props over the BODY shadow actually
             * under the camera. Reject floor-sized owners that cannot buy at
             * least modest detail, then rank the remaining real shadow rays
             * primarily by camera relevance. Keep only a small density bonus.
             */
            if(useful_zoom < 1.5f)
                continue;
            relevance_divisor = 1.0f + 0.50f * center_metric;
            stretch_bonus = 1.0f + 0.20f * ((stretch - 1.0f) / 31.0f);
            {
                br_float density_bonus = 1.0f + 0.10f *
                    ((useful_zoom > 8.0f ? 8.0f : useful_zoom) - 1.0f) / 7.0f;
                score = density_bonus * stretch_bonus / relevance_divisor;
            }

            if(selected_owner == NULL || score > best_score) {
                selected_owner = owner;
                best_score = score;
                best_useful_zoom = useful_zoom;
            }
            if(owner->key == previous_detail_owner_key) {
                previous_owner = owner;
                previous_score = score;
                previous_useful_zoom = useful_zoom;
            }
        }

        if(previous_owner != NULL && selected_owner != NULL &&
           previous_score >= best_score * 0.70f) {
            selected_owner = previous_owner;
            best_useful_zoom = previous_useful_zoom;
            kept_previous = BR_TRUE;
        }

        if(selected_owner != NULL) {
            detail_source = &selected_owner->fit;
            previous_detail_owner_key = selected_owner->key;
        } else {
            previous_detail_owner_key = NULL;
        }

        dmin_u = detail_source->min_u;
        dmax_u = detail_source->max_u;
        dmin_v = detail_source->min_v;
        dmax_v = detail_source->max_v;
        br_float min_detail_half_u = half_u / detail_max_zoom;
        br_float min_detail_half_v = half_v / detail_max_zoom;

        if(dmin_u < base_min_u) dmin_u = base_min_u;
        if(dmax_u > base_max_u) dmax_u = base_max_u;
        if(dmin_v < base_min_v) dmin_v = base_min_v;
        if(dmax_v > base_max_v) dmax_v = base_max_v;

        if(dmin_u <= dmax_u && dmin_v <= dmax_v) {
            dcenter_u = (dmin_u + dmax_u) * 0.5f;
            dcenter_v = (dmin_v + dmax_v) * 0.5f;
            dhalf_u = (dmax_u - dmin_u) * 0.5f * detail_padding;
            dhalf_v = (dmax_v - dmin_v) * 0.5f * detail_padding;

            if(dhalf_u < min_detail_half_u) dhalf_u = min_detail_half_u;
            if(dhalf_v < min_detail_half_v) dhalf_v = min_detail_half_v;
            if(dhalf_u > half_u) dhalf_u = half_u;
            if(dhalf_v > half_v) dhalf_v = half_v;

            if(dcenter_u - dhalf_u < base_min_u) dcenter_u = base_min_u + dhalf_u;
            if(dcenter_u + dhalf_u > base_max_u) dcenter_u = base_max_u - dhalf_u;
            if(dcenter_v - dhalf_v < base_min_v) dcenter_v = base_min_v + dhalf_v;
            if(dcenter_v + dhalf_v > base_max_v) dcenter_v = base_max_v - dhalf_v;

            out_detail_fit->v[0] = dcenter_u;
            out_detail_fit->v[1] = dcenter_v;
            out_detail_fit->v[2] = dhalf_u;
            out_detail_fit->v[3] = dhalf_v;

            {
                static const void *last_detail_owner_key = NULL;
                static br_uint_32 detail_diag_count = 0;
                const void *selected_key = selected_owner != NULL ? selected_owner->key : NULL;
                br_boolean owner_changed = selected_key != last_detail_owner_key;

                /* Log startup plus every owner transition indefinitely. The
                 * old 192-line cap expired ten minutes before the user's
                 * captured rough/smooth transition and hid the useful event. */
                if(detail_diag_count < 24 || owner_changed) {
                    BrWarning("SHADOW detail_focus_v262 n=%u changed=%u union_models=%u union_points=%u owners=%u selected_key=%p full_models=%u full_points=%u visible_models=%u visible_points=%u max_visible_t=%.5g center_metric=%.5g useful_zoom=%.4g score=%.5g kept_previous=%u center=(%.7g,%.7g) half=(%.7g,%.7g) base_half=(%.7g,%.7g) angular_zoom=(%.3fx,%.3fx)",
                              (unsigned)detail_diag_count++, (unsigned)owner_changed,
                              (unsigned)detail_fit.model_count, (unsigned)detail_fit.point_count,
                              (unsigned)detail_owners.count, selected_key,
                              selected_owner != NULL ? (unsigned)selected_owner->fit.model_count : (unsigned)detail_fit.model_count,
                              selected_owner != NULL ? (unsigned)selected_owner->fit.point_count : (unsigned)detail_fit.point_count,
                              selected_owner != NULL ? (unsigned)selected_owner->visible_model_count : 0u,
                              selected_owner != NULL ? (unsigned)selected_owner->visible_point_count : 0u,
                              selected_owner != NULL ? (double)selected_owner->max_visible_t : 1.0,
                              selected_owner != NULL ? (double)selected_owner->min_center_metric : 1000000.0,
                              (double)best_useful_zoom,
                              (double)(kept_previous ? previous_score : best_score),
                              (unsigned)kept_previous,
                              (double)dcenter_u, (double)dcenter_v,
                              (double)dhalf_u, (double)dhalf_v,
                              (double)half_u, (double)half_v,
                              (double)(half_u / dhalf_u), (double)(half_v / dhalf_v));
                }
                last_detail_owner_key = selected_key;
            }
        }
    }

    {
        static br_uint_32 fit_diag_count;
        if(fit_diag_count < 192) {
            BrWarning("SHADOW fit_v228 n=%u models=%u points=%u tan_half=%.7g center=(%.7g,%.7g) half=(%.7g,%.7g) gain=(%.3fx,%.3fx) max_zoom=32",
                      (unsigned)fit_diag_count++, (unsigned)fit.model_count, (unsigned)fit.point_count, (double)tan_half,
                      (double)center_u, (double)center_v, (double)half_u, (double)half_v,
                      (double)(tan_half / half_u), (double)(tan_half / half_v));
        }
    }
    return BR_TRUE;
}
#endif

static void actorEnable(br_v1db_enable *e, br_actor *a)
{
    int i;

    ASSERT_MESSAGE("actorEnable NULL pointer to an actor", a != NULL);
    ASSERT(a->type == e->type);

    if(e->enabled == NULL)
        e->enabled = BrResAllocate(v1db.res, e->max * sizeof(*e->enabled), BR_MEMORY_ENABLED_ACTORS);

    /* BRender:
     * Look to see if actor is already enabled
     */
    for(i = 0; i < e->max; i++)
        if(e->enabled[i] == a)
            return;

    /* BRender:
     * Find a blank slot
     */
    for(i = 0; i < e->max; i++) {
        if(e->enabled[i] == NULL) {
            e->enabled[i] = a;
            e->count++;
            return;
        }
    }

    BR_ERROR1("too many enabled %ss", e->name);
}

static void actorDisable(br_v1db_enable *e, br_actor *a)
{
    int i;

    ASSERT_MESSAGE("actorDisable NULL pointer to an actor", a != NULL);
    ASSERT(a->type == e->type);

    if(e->enabled == NULL)
        return;

    /* BRender:
     * Find actor in table and remove it
     */
    for(i = 0; i < e->max; i++) {
        if(e->enabled[i] == a) {
            e->enabled[i] = NULL;
            e->count--;
            return;
        }
    }
}

/* BRender:
 * Add a light to the set that will illuminate the world
 */
void BR_PUBLIC_ENTRY BrLightEnable(br_actor *l)
{
    UASSERT_MESSAGE("BrLightEnable NULL pointer to a light actor", l != NULL);

    actorEnable(&v1db.enabled_lights, l);
}

/* BRender:
 * Remove a light from the set that will illuminate the world
 */
void BR_PUBLIC_ENTRY BrLightDisable(br_actor *l)
{

    UASSERT_MESSAGE("BrLightDisable NULL pointer to a light actor", l != NULL);
    actorDisable(&v1db.enabled_lights, l);
}

/* BRender:
 * Allow all lights to illuminate the current model
 */
br_error BrLightCullReset(void)
{
    br_token_value tv[2];
    br_uint_32     light_part;
    int            i;

    /* BRender:
     * Find the light in the list of enabled lights and set the appropriate
     * part of the light state.  N.B. the culled state is reset before each
     * model is rendered
     */
    if(v1db.enabled_lights.enabled == NULL)
        return BRE_FAIL;

    tv[0].t   = BRT_CULLED_B;
    tv[0].v.b = BR_FALSE;

    tv[1].t = BR_NULL_TOKEN;

    for(light_part = 0, i = 0; i < v1db.enabled_lights.max; i++) {

        if(v1db.enabled_lights.enabled[i] == NULL)
            continue;

        RendererPartSetMany(v1db.renderer, BRT_LIGHT, light_part, tv, NULL);

        light_part++;
    }

    return BRE_OK;
}

/* BRender:
 * Prevent a light from illuminating the current model
 */
br_error BR_PUBLIC_ENTRY BrLightModelCull(br_actor *light)
{
    br_token_value tv[2];
    br_uint_32     light_part;
    int            i;

    /* BRender:
     * Find the light in the list of enabled lights and set the appropriate
     * part of the light state.  N.B. the culled state is reset before each
     * model is rendered
     */
    if(v1db.enabled_lights.enabled == NULL)
        return BRE_FAIL;

    tv[0].t   = BRT_CULLED_B;
    tv[0].v.b = BR_TRUE;

    tv[1].t = BR_NULL_TOKEN;

    for(light_part = 0, i = 0; i < v1db.enabled_lights.max; i++) {

        if(v1db.enabled_lights.enabled[i] == NULL)
            continue;

        if(v1db.enabled_lights.enabled[i] == light) {

            RendererPartSetMany(v1db.renderer, BRT_LIGHT, light_part, tv, NULL);

            return BRE_OK;
        }

        light_part++;
    }

    return BRE_FAIL;
}

/* BRender:
 * Add a clip plane to world
 */
void BR_PUBLIC_ENTRY BrClipPlaneEnable(br_actor *c)
{

    UASSERT_MESSAGE("BrClipPlaneEnable NULL pointer to a clip plane actor", c != NULL);
    actorEnable(&v1db.enabled_clip_planes, c);
}

/* BRender:
 * Remove a clip plane
 */
void BR_PUBLIC_ENTRY BrClipPlaneDisable(br_actor *c)
{

    UASSERT_MESSAGE("BrClipPlaneDisable NULL pointer to a clip plane actor", c != NULL);

    actorDisable(&v1db.enabled_clip_planes, c);
}

/* BRender:
 * Add a horizon plane to world
 */
void BR_PUBLIC_ENTRY BrHorizonPlaneEnable(br_actor *h)
{
    UASSERT_MESSAGE("BrHorizonPlaneEnable NULL pointer to a horizon plane actor", h != NULL);

    actorEnable(&v1db.enabled_horizon_planes, h);
}

/* BRender:
 * Remove a horizon plane
 */
void BR_PUBLIC_ENTRY BrHorizonPlaneDisable(br_actor *h)
{

    UASSERT_MESSAGE("BrHorizonPlaneDisable NULL pointer to a horizon plane actor", h != NULL);

    actorDisable(&v1db.enabled_horizon_planes, h);
}

/* BRender:
 * Sets the new environment anchor
 *
 * Returns the previous value
 */
br_actor *BR_PUBLIC_ENTRY BrEnvironmentSet(br_actor *a)
{

    br_actor *old_a = v1db.enabled_environment;

    v1db.enabled_environment = a;

    return old_a;
}

/* BRender:
 * Build transforms between the view and a given actor
 */
static br_boolean setupView(br_matrix34 *view_to_this, br_matrix34 *this_to_view, br_matrix34 *world_to_view, br_int_32 w2vt,
                            br_actor *world, br_actor *a)
{
    br_matrix34 this_to_world;
    br_int_32   root_t, t;

    ASSERT_MESSAGE("setupView NULL pointer", view_to_this != NULL);
    ASSERT_MESSAGE("setupView NULL pointer", this_to_view != NULL);
    ASSERT_MESSAGE("setupView NULL pointer", world_to_view != NULL);
    ASSERT_MESSAGE("setupView NULL pointer", world != NULL);

    /* BRender:
     * Find this->world, fail if not in world
     */
    if(!ActorToRootTyped(a, world, &this_to_world, &root_t))
        return BR_FALSE;

    /* BRender:
     * Make this->view and invert it
     */
    BrMatrix34Mul(this_to_view, &this_to_world, world_to_view);
    t = BrTransformCombineTypes(root_t, w2vt);

    /* BRender:
     * Build view->light
     */
    if(BrTransformTypeIsLP(t))
        BrMatrix34LPInverse(view_to_this, this_to_view);
    else
        BrMatrix34Inverse(view_to_this, this_to_view);

    return BR_TRUE;
}

/* BRender:
 * Configure renderer with current enabled lights
 */
void BrSetupLights(br_actor *world, br_actor *camera, br_matrix34 *world_to_view, br_int_32 w2vt)
{
    br_matrix34       this_to_view, view_to_this;
    int               light_part = 0, l;
    br_token_value    tv[40], *tvp;
    br_vector3        view_position, view_direction;
    br_vector4_f      shadow_fit;
    br_vector4_f      shadow_detail_fit;
    br_light         *light;
    br_light_volume   new_volume;
    br_convex_region *temp_regions, *region, *new_region;
    br_vector4       *temp_planes, *plane, *new_plane;
    br_uint_32        i, j;

    ASSERT_MESSAGE("BrSetupLights NULL pointer to actor", world != NULL);
    ASSERT_MESSAGE("BrSetupLights NULL pointer to camera", camera != NULL);
    ASSERT_MESSAGE("BrSetupLights NULL pointer", world_to_view != NULL);

    if(v1db.enabled_lights.enabled == NULL)
        return;

    for(l = 0; l < v1db.enabled_lights.max; l++) {
        tvp = tv;

        if(v1db.enabled_lights.enabled[l] == NULL) {
            continue;
        }

        light = v1db.enabled_lights.enabled[l]->type_data;

        ASSERT_MESSAGE("Invalid light data", light != NULL);

        /* BRender:
         * Work out view<->light transforms - ignore light if not part of current hierachy
         */
        if(!setupView(&view_to_this, &this_to_view, world_to_view, w2vt, world, v1db.enabled_lights.enabled[l]))
            continue;

        /* BRender:
         * Construct generic parts of light
         */
        tvp->t     = BRT_COLOUR_RGB;
        tvp->v.rgb = light->colour;
        tvp++;

        tvp->t   = BRT_AS_SCALAR(COLOUR_R);
        tvp->v.s = BrFixedToScalar(BR_RED(light->colour) << 8);
        tvp++;

        tvp->t   = BRT_AS_SCALAR(COLOUR_G);
        tvp->v.s = BrFixedToScalar(BR_GRN(light->colour) << 8);
        tvp++;

        tvp->t   = BRT_AS_SCALAR(COLOUR_B);
        tvp->v.s = BrFixedToScalar(BR_BLU(light->colour) << 8);
        tvp++;

        tvp->t   = BRT_AS_SCALAR(ATTENUATION_C);
        tvp->v.s = light->attenuation_c;
        tvp++;

        tvp->t   = BRT_AS_SCALAR(ATTENUATION_L);
        tvp->v.s = light->attenuation_l;
        tvp++;

        tvp->t   = BRT_AS_SCALAR(ATTENUATION_Q);
        tvp->v.s = light->attenuation_q;
        tvp++;

        tvp->t   = BRT_SPACE_T;
        tvp->v.t = (light->type & BR_LIGHT_VIEW) ? BRT_VIEW : BRT_MODEL;
        tvp++;

#if BRENDER_LEGACY_3DMM_MODEL_ABI
        tvp->t   = Br4DMMShadowToken();
        tvp->v.b = (light->type & BR_LIGHT_SHADOW) != 0;
        tvp++;

        if((light->type & BR_LIGHT_SHADOW) != 0 && (light->type & BR_LIGHT_TYPE) == BR_LIGHT_SPOT) {
            br_scalar cone_outer = BR_FMOD(light->cone_outer, BR_SCALAR(1));
            br_float tan_half;

            if(cone_outer < BR_SCALAR(0.0))
                cone_outer += BR_SCALAR(1.0);
            if(cone_outer > BR_SCALAR(0.5))
                cone_outer = BR_SCALAR(0.5);
            tan_half = BrScalarToFloat(BR_TAN(cone_outer));

            tvp->t     = Br4DMMShadowViewToLightToken();
            tvp->v.m34 = &view_to_this;
            tvp++;

            shadow_fit.v[0] = 0.0f;
            shadow_fit.v[1] = 0.0f;
            shadow_fit.v[2] = 0.0f;
            shadow_fit.v[3] = 0.0f;
            shadow_detail_fit.v[0] = 0.0f;
            shadow_detail_fit.v[1] = 0.0f;
            shadow_detail_fit.v[2] = 0.0f;
            shadow_detail_fit.v[3] = 0.0f;
            (void)Br4DMMShadowComputeFit(world, camera, v1db.enabled_lights.enabled[l], tan_half,
                                         &shadow_fit, &shadow_detail_fit);
            tvp->t      = Br4DMMShadowFitToken();
            tvp->v.v4_f = &shadow_fit;
            tvp++;

            tvp->t      = Br4DMMShadowDetailFitToken();
            tvp->v.v4_f = &shadow_detail_fit;
            tvp++;
        }
#endif

        tvp->t = BRT_TYPE_T;

        switch(light->type & BR_LIGHT_TYPE) {

            case BR_LIGHT_POINT:
                tvp->v.t = BRT_POINT;
                break;

            case BR_LIGHT_DIRECT:
                tvp->v.t = BRT_DIRECT;
                break;

            case BR_LIGHT_SPOT:
                tvp->v.t = BRT_SPOT;
                break;

            case BR_LIGHT_AMBIENT:
                tvp->v.t = BRT_AMBIENT;
                break;
        }

        tvp++;

        /* BRender:
         * Set position and radius and enable radius culling, if
         * appropriate
         */
        if((light->type & BR_LIGHT_TYPE) == BR_LIGHT_POINT || (light->type & BR_LIGHT_TYPE) == BR_LIGHT_SPOT ||
           light->volume.regions != NULL || light->type & BR_LIGHT_LINEAR_FALLOFF) {

            /* BRender:
             * Transform position (0,0,0,1) into view space
             */
            BrVector3CopyMat34Row(&view_position, &this_to_view, 3);
            tvp->t   = BRT_AS_VECTOR3_SCALAR(POSITION);
            tvp->v.p = &view_position;
            tvp++;

            tvp->t   = BRT_AS_SCALAR(RADIUS_OUTER);
            tvp->v.s = light->radius_outer;
            tvp++;

            tvp->t   = BRT_AS_SCALAR(RADIUS_INNER);
            tvp->v.s = light->radius_inner;
            tvp++;

            /* BRender:
             * Enable radius culling if outer radius given
             */
            tvp->t   = BRT_RADIUS_CULL_B;
            tvp->v.b = light->radius_outer != BR_SCALAR(0.0);
            tvp++;

        } else {

            tvp->t   = BRT_RADIUS_CULL_B;
            tvp->v.b = BR_FALSE;
            tvp++;
        }

        /* BRender:
         * Set direction and cone angles and enable angle culling, if
         * appropriate
         */
        if((light->type & BR_LIGHT_TYPE) == BR_LIGHT_DIRECT || (light->type & BR_LIGHT_TYPE) == BR_LIGHT_SPOT || light->volume.regions != NULL) {

            /* BRender:
             * Transform direction (0,0,1,0) into view space -
             * use T(I(l_to_v)) - or column 2 of view_to_this
             */
            BrVector3CopyMat34Col(&view_direction, &view_to_this, 2);
            BrVector3Normalise(&view_direction, &view_direction);
            tvp->t   = BRT_AS_VECTOR3_SCALAR(DIRECTION);
            tvp->v.p = &view_direction;
            tvp++;

            if((light->type & BR_LIGHT_TYPE) == BR_LIGHT_SPOT) {
                br_scalar cone_outer = BR_FMOD(light->cone_outer, BR_SCALAR(1));
                br_scalar cone_inner = BR_FMOD(light->cone_inner, BR_SCALAR(1));

                if(cone_outer < BR_SCALAR(0.0))
                    cone_outer += BR_SCALAR(1.0);

                if(cone_inner < BR_SCALAR(0.0))
                    cone_inner += BR_SCALAR(1.0);

                if(cone_outer > BR_SCALAR(0.5))
                    cone_outer = BR_SCALAR(0.5);

                if(cone_inner > cone_outer)
                    cone_inner = cone_outer;

                tvp->t   = BRT_AS_SCALAR(SPOT_OUTER);
                tvp->v.s = BR_COS(cone_outer);
                tvp++;

                tvp->t   = BRT_AS_SCALAR(SPOT_INNER);
                tvp->v.s = BR_COS(cone_inner);
                tvp++;

                tvp->t   = BRT_ANGLE_OUTER_A;
                tvp->v.a = cone_outer;
                tvp++;

                tvp->t   = BRT_ANGLE_INNER_A;
                tvp->v.a = cone_inner;
                tvp++;

                tvp->t   = BRT_ANGLE_CULL_B;
                tvp->v.b = BR_TRUE;
                tvp++;

            } else {

                tvp->t   = BRT_ANGLE_CULL_B;
                tvp->v.b = BR_FALSE;
                tvp++;
            }

        } else {

            tvp->t   = BRT_ANGLE_CULL_B;
            tvp->v.b = BR_FALSE;
            tvp++;
        }

        /* BRender:
         * Pass down type of attenuation
         */
        tvp->t   = BRT_ATTENUATION_TYPE_T;
        tvp->v.t = light->type & BR_LIGHT_LINEAR_FALLOFF ? BRT_RADII : BRT_QUADRATIC;
        tvp++;

        /* BRender:
         * Pass down a hint describing the complexity of the attenuation
         */
        tvp->t   = BRT_ATTENUATION_HINT_T;
        tvp->v.t = light->attenuation_q != BR_SCALAR(0.0)   ? BRT_QUADRATIC
                   : light->attenuation_l != BR_SCALAR(0.0) ? BRT_LINEAR
                                                            : BRT_CONSTANT;
        tvp++;

        /* BRender:
         * Transform lighting volume into view space and pass down
         */
        tvp->t = BRT_LIGHTING_VOLUME_P;

        if(light->volume.regions != NULL) {

            temp_regions = BrResAllocate(v1db.res, sizeof(*temp_regions) * light->volume.nregions, BR_MEMORY_OBJECT_DATA);

            if(temp_regions != NULL)

                for(region = light->volume.regions, new_region = temp_regions, i = 0; i < light->volume.nregions; i++, region++, new_region++) {

                    temp_planes = BrResAllocate(temp_regions, sizeof(*temp_planes) * region->nplanes, BR_MEMORY_OBJECT_DATA);

                    if(temp_planes == NULL) {

                        BrResFree(temp_regions);
                        temp_regions = NULL;

                        break;
                    }

                    for(plane = region->planes, new_plane = temp_planes, j = 0; j < region->nplanes; j++, plane++, new_plane++)
                        BrMatrix34ApplyPlaneEquation(new_plane, plane, &this_to_view);

                    new_region->planes  = temp_planes;
                    new_region->nplanes = region->nplanes;
                }

            new_volume.falloff_distance = light->volume.falloff_distance;
            new_volume.regions          = temp_regions;
            new_volume.nregions         = light->volume.nregions;

            tvp->v.p = &new_volume;

        } else {

            temp_regions = NULL;
            tvp->v.p     = NULL;
        }

        tvp++;

        /* BRender:
         * Send finished token list off to renderer
         */
        tvp->t = BR_NULL_TOKEN;
        RendererPartSetMany(v1db.renderer, BRT_LIGHT, light_part, tv, NULL);
        light_part++;

        if(temp_regions != NULL)
            BrResFree(temp_regions);
    }

    /* BRender:
     * Disable any previous remaining lights
     */
    tv[0].t   = BRT_TYPE_T;
    tv[0].v.t = BRT_NONE;
    tv[1].t   = BR_NULL_TOKEN;

    for(; light_part < v1db.max_light; light_part++)
        RendererPartSetMany(v1db.renderer, BRT_LIGHT, light_part, tv, NULL);

    v1db.max_light = light_part;
}

/* BRender:
 * Configure renderer with current enabled clip planes
 */
void BrSetupClipPlanes(br_actor *world, br_matrix34 *world_to_view, br_int_32 w2vt, br_matrix4 *view_to_screen)
{
    br_matrix34    this_to_view, view_to_this;
    br_matrix4     screen_to_view, tmp4, screen_to_this;
    int            light_part = 0, i;
    br_int_32      clip_part;
    br_token_value tv[3];
    br_vector4     sp;

    ASSERT_MESSAGE("BrSetupClipPlanes NULL pointer to actor", world != NULL);
    ASSERT_MESSAGE("BrSetupClipPlanes NULL pointer", world_to_view != NULL);
    ASSERT_MESSAGE("BrSetupClipPlanes NULL pointer", view_to_screen != NULL);

    tv[0].t   = BRT_TYPE_T;
    tv[0].v.t = BRT_PLANE;
    tv[1].t   = BRT_AS_VECTOR4_SCALAR(PLANE);
    tv[1].v.p = &sp;
    tv[2].t   = BR_NULL_TOKEN;

    clip_part = 0;

    if(v1db.enabled_clip_planes.enabled == NULL)
        return;

    if(v1db.enabled_clip_planes.count)
        BrMatrix4Inverse(&screen_to_view, view_to_screen);

    for(i = 0; i < v1db.enabled_clip_planes.max; i++) {
        if(v1db.enabled_clip_planes.enabled[i] == NULL) {
            continue;
        }

        if(!setupView(&view_to_this, &this_to_view, world_to_view, w2vt, world, v1db.enabled_clip_planes.enabled[i]))
            continue;

        /* BRender:
         * Make screen->plane
         */
        BrMatrix4Copy34(&tmp4, &view_to_this);

        BrMatrix4Mul(&screen_to_this, &screen_to_view, &tmp4);

        /* BRender:
         * Push plane through to screen space
         */
        BrMatrix4TApply(&sp, v1db.enabled_clip_planes.enabled[i]->type_data, &screen_to_this);

        RendererPartSetMany(v1db.renderer, BRT_CLIP, clip_part, tv, NULL);
        clip_part++;
    }

    tv[0].t   = BRT_TYPE_T;
    tv[0].v.t = BRT_NONE;
    tv[1].t   = BR_NULL_TOKEN;

    for(; clip_part < v1db.max_clip; clip_part++)
        RendererPartSetMany(v1db.renderer, BRT_CLIP, clip_part, tv, NULL);

    v1db.max_clip = clip_part;
}

/* BRender:
 * Configure renderer with current environment
 */
void BrSetupEnvironment(br_actor *world, br_matrix34 *world_to_view, br_int_32 w2vt)
{
    br_matrix34 view_to_this, this_to_view;
    br_token    h = BRT_DONT_CARE;

    ASSERT_MESSAGE("BrSetupEnvironment NULL pointer to actor", world != NULL);
    ASSERT_MESSAGE("BrSetupEnvironment NULL pointer", world_to_view != NULL);

    if(v1db.enabled_environment) {

        if(v1db.enabled_environment != world) {
            if(setupView(&view_to_this, &this_to_view, world_to_view, w2vt, world, v1db.enabled_environment))
                h = BRT_NONE;

        } else {
            if(BrTransformTypeIsLP(w2vt))
                BrMatrix34LPInverse(&view_to_this, world_to_view);
            else
                BrMatrix34Inverse(&view_to_this, world_to_view);

            h = BRT_NONE;
        }
    }

    /* BRender:
     * Send to renderer
     */
    if(h != BRT_DONT_CARE)
        RendererPartSet(v1db.renderer, BRT_MATRIX, 0, BRT_AS_MATRIX34_SCALAR(VIEW_TO_ENVIRONMENT), (br_value){.m34 = &view_to_this});
    RendererPartSet(v1db.renderer, BRT_MATRIX, 0, BRT_VIEW_TO_ENVIRONMENT_HINT_T, (br_value){.t = h});
}

/* BRender:
 * Generate the current horizon planes
 */
void BrSetupHorizons(br_actor *world, br_matrix34 *world_to_view, br_int_32 w2vt)
{
    /* BRender: XXX */
}

/* BRender:
 * See if this is an 'enabled' actor
 */

/* BRender:
 * Check if an actor is enabled, prior to freeing
 *
 * This is not per the manual, which states that the app.
 * should disable actors before freeing them, but it does let
 * code that 'just' worked under 1.1 to function
 */
void BrActorEnableCheck(br_actor *a)
{
    /* BRender:
     * Is the actor the environment anchor?
     */
    if(a == v1db.enabled_environment)
        v1db.enabled_environment = NULL;

    /* BRender:
     * See if actor of any specific type
     */
    switch(a->type) {
        case BR_ACTOR_LIGHT:
            actorDisable(&v1db.enabled_lights, a);
            break;
        case BR_ACTOR_CLIP_PLANE:
            actorDisable(&v1db.enabled_clip_planes, a);
            break;
        case BR_ACTOR_HORIZON_PLANE:
            actorDisable(&v1db.enabled_horizon_planes, a);
            break;
    }
}
