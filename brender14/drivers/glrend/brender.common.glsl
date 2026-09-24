#define MAX_LIGHTS                   48 /* Must match up with BRender */
#define MAX_CLIP_PLANES              6  /* Must match up with BRender */
#define SPECULARPOW_CUTOFF           0.6172
#define BR_SCALAR_EPSILON            1.192092896e-7f

#define UV_SOURCE_MODEL              0
#define UV_SOURCE_ENV_L              1
#define UV_SOURCE_ENV_I              2
#define UV_SOURCE_GEOMETRY_X         3
#define UV_SOURCE_GEOMETRY_Y         4
#define UV_SOURCE_GEOMETRY_Z         5

#define TEXTURE_MODE_NORMAL          0u
#define TEXTURE_MODE_INDEX           1u
#define TEXTURE_MODE_INDEX_FILTER    2u

#define BRT_AMBIENT                  0u
#define BRT_DIRECT                   1u
#define BRT_POINT                    2u
#define BRT_SPOT                     3u

#define BRT_QUADRATIC                0u /* Quadratic attenuation, i.e. standard 1/clq ... */
#define BRT_RADII                    1u /* Radial attenuation, i.e. linear falloff. */

#define COLOUR_SOURCE_GEOMETRY       0u
#define COLOUR_SOURCE_SURFACE        1u

#define SHADING_MODE_FLAT            0u
#define SHADING_MODE_GOURAUD         1u
#define SHADING_MODE_PHONG           2u

#define DEBUG_DISABLE_LIGHTS            0
#define DEBUG_DISABLE_LIGHT_AMBIENT     0
#define DEBUG_DISABLE_LIGHT_DIRECTIONAL 0
#define DEBUG_DISABLE_LIGHT_POINT       0
#define DEBUG_DISABLE_LIGHT_SPOT        0

layout(std140, binding=0) uniform br_scene_state
{
    vec4 eye_view; /* Eye position in view-space */
    uvec4 light_info[MAX_LIGHTS];
    vec4 light_positions[MAX_LIGHTS];
    vec4 light_directions[MAX_LIGHTS];
    vec4 light_halfs[MAX_LIGHTS];
    vec4 light_colours[MAX_LIGHTS];
    vec4 light_atten[MAX_LIGHTS];
    vec4 light_radii[MAX_LIGHTS];
    vec4 clip_planes[MAX_CLIP_PLANES];
    vec4 ambient_colour;
    uvec4 light_start;
    uvec4 light_end;
    uint num_clip_planes;
    bool use_ambient_colour;
    vec4 shadow_info;  /* enabled, light index, tan(half-FOV), near */
    vec4 shadow_info2; /* far, depth bias, reserved, reserved */
    vec4 shadow_world_up_view;
    vec4 shadow_world_right_view;
    vec4 shadow_detail_fit;
    vec4 shadow_detail_info; /* enabled, render_detail_pass, zoom, reserved */
};

uniform sampler2D shadow_texture;
uniform usampler2D shadow_owner_texture;
uniform sampler2D shadow_blocker_texture;
uniform sampler2D shadow_detail_texture;

layout(std140, binding=1) uniform br_model_state
{
    mat4 model_view;
    mat4 projection;
    mat4 mvp;
    mat4 shadow_model_to_light;
    mat4 normal_matrix;
    mat4 environment;
    mat4 map_transform;
    vec4 surface_colour;
    vec4 eye_m; /* Eye position in model-space */
    vec4 fog_colour;
    vec2 fog_range; /* (min, max) */

    float ka; /* Ambient mod */
    float ks; /* Specular mod (doesn't seem to be used by Croc) */
    float kd; /* Diffuse mod */
    float power;
    bool lighting;      /* BRT_LIGHTING_B */
    bool prelighting;   /* BRT_PRELIGHTING_B */
    int colour_source;
    int uv_source;
    bool disable_colour_key;
    uint texture_mode;
    bool enable_fog;
    float fog_scale;
    int shading_mode;
    uvec4 shadow_owner_info; /* x = logical 3DMM object owner */
};

vec4 shadowProjectLightFit(in vec3 p_light, in vec4 fit)
{
    float z = -p_light.z;
    float tan_half = max(shadow_info.z, 0.001);
    float near_z = shadow_info.w;
    float far_z = shadow_info2.x;
    float a = (far_z + near_z) / (far_z - near_z);
    float b = (-2.0 * far_z * near_z) / (far_z - near_z);
    vec2 fit_center = fit.xy;
    vec2 fit_half = fit.zw;
    if(fit_half.x <= 0.0 || fit_half.y <= 0.0)
        fit_half = vec2(tan_half);

    return vec4((p_light.x - fit_center.x * z) / fit_half.x,
                (p_light.y - fit_center.y * z) / fit_half.y,
                a * z + b,
                z);
}

vec4 shadowProjectLight(in vec3 p_light)
{
    /*
     * v237: the depth shader can switch to the nested detail projection while
     * the colour shader always keeps both base and detail fits available.
     */
    vec4 fit = shadow_detail_info.y > 0.5 ? shadow_detail_fit : shadow_world_up_view;
    return shadowProjectLightFit(p_light, fit);
}

vec2 shadowFitUVFromSlope(in vec2 slope, in vec4 fit)
{
    float tan_half = max(shadow_info.z, 0.001);
    vec2 fit_center = fit.xy;
    vec2 fit_half = fit.zw;
    if(fit_half.x <= 0.0 || fit_half.y <= 0.0)
        fit_half = vec2(tan_half);
    return ((slope - fit_center) / fit_half) * 0.5 + 0.5;
}

vec2 shadowSampleSlope(in vec2 sample_uv, in vec4 fit)
{
    float tan_half = max(shadow_info.z, 0.001);
    vec2 fit_center = fit.xy;
    vec2 fit_half = fit.zw;
    if(fit_half.x <= 0.0 || fit_half.y <= 0.0)
        fit_half = vec2(tan_half);
    return fit_center + (sample_uv * 2.0 - 1.0) * fit_half;
}

float shadowReceiverPlaneDepthFit(in vec2 sample_uv, in vec3 p_light, in vec3 n_light, in vec4 fit)
{
    vec2 slope = shadowSampleSlope(sample_uv, fit);
    vec3 ray = vec3(slope.x, slope.y, -1.0);
    float denom = dot(n_light, ray);
    if(abs(denom) < 1.0e-7)
        return -1.0;

    float z = dot(n_light, p_light) / denom;
    if(z <= 0.0)
        return -1.0;

    vec3 plane_point = ray * z;
    vec4 plane_clip = shadowProjectLightFit(plane_point, fit);
    if(plane_clip.w <= 0.0)
        return -1.0;

    return (plane_clip.z / plane_clip.w) * 0.5 + 0.5;
}

vec3 shadowUnprojectLightFit(in vec2 sample_uv, in float stored_depth, in vec4 fit)
{
    float near_z = shadow_info.w;
    float far_z = shadow_info2.x;
    float a = (far_z + near_z) / (far_z - near_z);
    float b = (-2.0 * far_z * near_z) / (far_z - near_z);
    float ndc_z = stored_depth * 2.0 - 1.0;
    float denom = ndc_z - a;
    if(abs(denom) < 1.0e-9)
        return vec3(0.0);

    float z = b / denom;
    vec2 slope = shadowSampleSlope(sample_uv, fit);
    return vec3(slope.x * z, slope.y * z, -z);
}

float shadowVisibilityFit(in vec3 p_light, in vec3 receiver_plane_normal,
                          in bool use_detail, in vec4 active_fit,
                          in vec4 active_clip, in ivec2 active_size)
{
    vec3 ndc = active_clip.xyz / active_clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float current_depth = ndc.z * 0.5 + 0.5;
    float bias = shadow_info2.y;
    vec2 texel = 1.0 / vec2(active_size);
    uint receiver_owner = shadow_owner_info.x;
    bool flush_overlap_v230 = (shadow_owner_info.y & 1u) != 0u;

    float visible = 0.0;
    int occluded_samples = 0;
    bool center_occluded = false;

    vec3 world_up_light = normalize(shadow_world_right_view.xyz);
    float horizontal_receiver = abs(dot(receiver_plane_normal, world_up_light));
    float ray_height_delta = dot(world_up_light, vec3(0.0, 0.0, -1.0));
    const float coplanar_height_epsilon = 0.015625;
    const float blocker_depth_epsilon = 2.0 / 65535.0;
    ivec2 base_size = textureSize(shadow_texture, 0);

    for(int y = -1; y <= 1; ++y) {
        for(int x = -1; x <= 1; ++x) {
            vec2 sample_uv = uv + vec2(float(x), float(y)) * texel;
            ivec2 sample_coord = ivec2(floor(sample_uv * vec2(active_size)));
            sample_coord = clamp(sample_coord, ivec2(0), active_size - ivec2(1));
            vec2 sample_center_uv = (vec2(sample_coord) + vec2(0.5)) * texel;
            vec2 geometric_sample_uv = flush_overlap_v230 ? sample_uv : sample_center_uv;

            float stored_depth;
            if(use_detail) {
                stored_depth = flush_overlap_v230 ?
                    texture(shadow_detail_texture, sample_uv).r :
                    texelFetch(shadow_detail_texture, sample_coord, 0).r;
            } else {
                stored_depth = flush_overlap_v230 ?
                    texture(shadow_texture, sample_uv).r :
                    texelFetch(shadow_texture, sample_coord, 0).r;
            }

            /*
             * Owner and blocker semantics remain in the proven base maps. Map
             * this active/detail sample ray back into the base fit before
             * reading those layers. This lets the detail cascade stay depth-only
             * and therefore affordable while preserving valid-caster detection
             * and Object Properties shadow-stop behaviour.
             */
            vec2 sample_slope = shadowSampleSlope(geometric_sample_uv, active_fit);
            vec2 base_sample_uv = shadowFitUVFromSlope(sample_slope, shadow_world_up_view);
            ivec2 base_coord = ivec2(floor(base_sample_uv * vec2(base_size)));
            base_coord = clamp(base_coord, ivec2(0), base_size - ivec2(1));
            uint caster_owner = flush_overlap_v230 ?
                texture(shadow_owner_texture, base_sample_uv).r :
                texelFetch(shadow_owner_texture, base_coord, 0).r;

            /*
             * v269: keep v268 self-shadowing, but distinguish real same-object
             * occlusion from the receiver re-hitting its own quantised shadow
             * depth. Different-owner shadows retain the proven four-LSB bias.
             */
            bool empty_caster = !use_detail && caster_owner == 0u;
            bool exact_owner = receiver_owner > 0u && receiver_owner < 255u &&
                               caster_owner == receiver_owner;

            float receiver_plane_depth = shadowReceiverPlaneDepthFit(geometric_sample_uv, p_light,
                                                                      receiver_plane_normal, active_fit);
            float compare_depth = receiver_plane_depth >= 0.0 ? receiver_plane_depth : current_depth;
            const float same_owner_bias = 64.0 / 16777216.0;
            float compare_bias = exact_owner ? max(bias, same_owner_bias) : bias;
            bool occluded = !empty_caster && compare_depth - compare_bias > stored_depth;

            if(occluded) {
                float blocker_depth = flush_overlap_v230 ?
                    texture(shadow_blocker_texture, base_sample_uv).r :
                    texelFetch(shadow_blocker_texture, base_coord, 0).r;
                bool blocker_between = blocker_depth < 1.0 - blocker_depth_epsilon &&
                                       blocker_depth > stored_depth + blocker_depth_epsilon &&
                                       blocker_depth < compare_depth - blocker_depth_epsilon;
                if(blocker_between)
                    occluded = false;
            }

            if(occluded && horizontal_receiver > 0.985 && abs(ray_height_delta) > 0.05) {
                vec3 caster_light = shadowUnprojectLightFit(geometric_sample_uv, stored_depth, active_fit);
                float caster_height_delta = dot(world_up_light, caster_light - p_light);

                if((ray_height_delta < 0.0 && caster_height_delta <= coplanar_height_epsilon) ||
                   (ray_height_delta > 0.0 && caster_height_delta >= -coplanar_height_epsilon))
                    occluded = false;
            }

            if(occluded) {
                ++occluded_samples;
                if(x == 0 && y == 0)
                    center_occluded = true;
            } else {
                visible += 1.0;
            }
        }
    }

    int required_occluded = horizontal_receiver > 0.985 ? (flush_overlap_v230 ? 5 : 7) : 5;
    if(!center_occluded || occluded_samples < required_occluded)
        return 1.0;

    return visible / 9.0;
}

float shadowVisibility(in vec3 p_light, in vec3 n_light, in uint light_index)
{
    if(shadow_info.x < 0.5 || uint(shadow_info.y + 0.5) != light_index)
        return 1.0;

    vec4 base_clip = shadowProjectLightFit(p_light, shadow_world_up_view);
    if(base_clip.w <= 0.0)
        return 1.0;

    vec3 base_ndc = base_clip.xyz / base_clip.w;
    if(base_ndc.x < -1.0 || base_ndc.x > 1.0 ||
       base_ndc.y < -1.0 || base_ndc.y > 1.0 ||
       base_ndc.z < -1.0 || base_ndc.z > 1.0)
        return 1.0;

    /*
     * v271: receiver-plane reconstruction must use the rasterised triangle's
     * actual geometric plane, not the interpolated/smoothed lighting normal.
     * 4DMM's modern path forces legacy materials through per-fragment Phong
     * lighting, so the old n_light value can bend continuously across a BODY
     * while the shadow map still contains flat triangle planes. That mismatch
     * is exactly capable of producing the residual triangle-shaped self-shadow
     * patches exposed by v268/v269.
     *
     * Fragment derivatives recover the real light-space triangle plane without
     * changing the visible lighting normal. Vertex-stage callers retain the old
     * normal fallback, though the 4DMM Phong path evaluates shadows here in the
     * fragment stage.
     */
    vec3 receiver_plane_normal = normalize(n_light);
#ifdef BRENDER_FRAGMENT_STAGE
    {
        vec3 dpdx = dFdx(p_light);
        vec3 dpdy = dFdy(p_light);
        vec3 geometric_normal = cross(dpdx, dpdy);
        float geometric_len2 = dot(geometric_normal, geometric_normal);
        if(geometric_len2 > 1.0e-16)
            receiver_plane_normal = geometric_normal * inversesqrt(geometric_len2);
    }
#endif

    ivec2 base_size = textureSize(shadow_texture, 0);

    /*
     * v271: the v254+ detail cascade previously switched from the base map to
     * the high-density map at one exact crop boundary. The two maps have very
     * different angular texel densities, so a long shadow crossing that line
     * can visibly jump resolution within only a few feet of receiver space.
     * Keep both maps unchanged, but cross-fade their final visibility over the
     * outer 4% of the detail crop. Outside that narrow band only one 3x3 map is
     * sampled, so the additional cost is confined to the handoff region.
     */
    if(shadow_detail_info.x > 0.5) {
        vec4 detail_clip = shadowProjectLightFit(p_light, shadow_detail_fit);
        if(detail_clip.w > 0.0) {
            vec3 detail_ndc = detail_clip.xyz / detail_clip.w;
            ivec2 detail_size = textureSize(shadow_detail_texture, 0);
            vec2 detail_uv = detail_ndc.xy * 0.5 + 0.5;
            vec2 detail_guard = vec2(2.0) / vec2(max(detail_size, ivec2(1)));

            if(detail_ndc.z >= -1.0 && detail_ndc.z <= 1.0 &&
               all(greaterThanEqual(detail_uv, detail_guard)) &&
               all(lessThanEqual(detail_uv, vec2(1.0) - detail_guard))) {
                float edge_distance = min(min(detail_uv.x, 1.0 - detail_uv.x),
                                          min(detail_uv.y, 1.0 - detail_uv.y));
                float guard_edge = max(detail_guard.x, detail_guard.y);
                const float handoff_width = 0.04;
                float handoff_end = min(0.125, guard_edge + handoff_width);

                if(edge_distance >= handoff_end)
                    return shadowVisibilityFit(p_light, receiver_plane_normal, true,
                                               shadow_detail_fit, detail_clip, detail_size);

                float base_visibility = shadowVisibilityFit(p_light, receiver_plane_normal, false,
                                                            shadow_world_up_view, base_clip, base_size);
                if(edge_distance <= guard_edge)
                    return base_visibility;

                float detail_visibility = shadowVisibilityFit(p_light, receiver_plane_normal, true,
                                                              shadow_detail_fit, detail_clip, detail_size);
                float detail_weight = smoothstep(guard_edge, handoff_end, edge_distance);
                return mix(base_visibility, detail_visibility, detail_weight);
            }
        }
    }

    return shadowVisibilityFit(p_light, receiver_plane_normal, false,
                               shadow_world_up_view, base_clip, base_size);
}

float calculateAttenuation(in uint i, in float dist)
{
    const float attenuation_c = light_atten[i][1];
    const float attenuation_l = light_atten[i][2];
    const float attenuation_q = light_atten[i][3];

    return 1.0 / (attenuation_c + (attenuation_l * dist) + (attenuation_q * dist * dist));
}

float calculateAttenuationRadii(in uint i, in float dist, in float intensity)
{
    const float radius_inner = light_radii[i][2];
    const float radius_outer = light_radii[i][3];

    /*
     * NB: radius_outer != radius_inner is enforced CPU-side.
     */
    float t = clamp((dist - radius_inner) / (radius_outer - radius_inner), 0.0, 1.0);
    return intensity * (1.0 - t);
}

/*
 * Radial ambient lights, who ever thought such things could be?
 * See softrend/light24.c, lightingColourAmbientRadii()
 */
void lightingColourAmbientRadii(in vec3 p, in vec3 n, in uint i, inout vec3 outA, inout vec3 outD, inout vec3 outS)
{
    const float intensity    = light_atten[i][0];
    const float radius_outer = light_radii[i][3];
    const vec3  position     = light_positions[i].xyz;

    float atten = 1.0f;

    vec3 dirn = position - p;
    float dist = length(dirn);

    if(dist >= radius_outer)
        return;

    atten = calculateAttenuationRadii(i, dist, intensity);

    // FIXME: should the intensity be multiplied here?
    outA += ka * intensity * light_colours[i].xyz * atten;
}

void lightingColourDirect(in vec3 p, in vec3 n, in uint i, inout vec3 outA, inout vec3 outD, inout vec3 outS)
{
    const float intensity = light_atten[i][0];
    const vec3  colour    = light_colours[i].rgb;
    const vec3  direction = light_directions[i].xyz;

    float diffDot = max(dot(n, direction), 0.0);
    outD += diffDot * kd * colour; /* NB: Intensity is scaled into the direction CPU-side (in cache.c) */

    if(ks > 0.0) {
        float specDot = max(dot(n, light_halfs[i].xyz), 0.0);
        if(specDot > 0.0) {
            outS += ks * intensity * colour * pow(specDot, power);
        }
    }
}

void lightingColourPoint(in vec3 p, in vec3 n, in uint i, inout vec3 outA, inout vec3 outD, inout vec3 outS)
{
    const uint  attenuation_type = light_info[i][1];
    const float intensity        = light_atten[i][0];
    const vec3  colour           = light_colours[i].rgb;
    const float radius_outer     = light_radii[i][3];
    const vec3  position         = light_positions[i].xyz;

    vec3 dirn = position - p;
    float dist_sqr = dot(dirn, dirn);

    if(attenuation_type == BRT_RADII && dist_sqr >= radius_outer * radius_outer)
        return;

    float dist = sqrt(dist_sqr);
    float atten = 0.0f;

    if(attenuation_type == BRT_RADII) {
        atten = calculateAttenuationRadii(i, dist, intensity);
    } else {
        atten = calculateAttenuation(i, dist);
    }

    if(atten <= 0)
        return;

    vec3 dirn_norm = dirn / dist;

    float diffDot = max(dot(n, dirn_norm), 0.0);
    outD += diffDot * kd * colour * atten;

    if(ks > 0.0) {
        float specDot = max(dot(n, normalize(eye_view.xyz + dirn_norm)), 0.0);
        if(specDot > 0.0) {
            outS += ks * colour * pow(specDot, power) * atten;
        }
    }
}

void lightingColourSpot(in vec3 p, in vec3 n, in vec3 shadow_p, in vec3 shadow_n, in uint i, inout vec3 outA, inout vec3 outD, inout vec3 outS)
{
    const float spot_inner_cos = light_radii[i][0];
    const float spot_outer_cos = light_radii[i][1];
    const vec3  position       = light_positions[i].xyz;
    const vec3  direction      = light_directions[i].xyz;

    /*
     * FIXME: We're calculating this twice (in lightingColourPoint).
     */
    vec3 dirn_norm = normalize(position - p);

    /*
     * NB: To test this, stick a spot light on a camera and see if you see it.
     */
    float spotDot = dot(dirn_norm, direction);
    if(spotDot <= spot_outer_cos)
        return;

    float cutoff = 1.0;
    float innerOuterDiff = spot_inner_cos - spot_outer_cos;

    if(innerOuterDiff != 0.0) {
        cutoff = clamp((spotDot - spot_outer_cos) / innerOuterDiff, 0.0, 1.0);
    }

    /*
     * A spot light is just a point light with a cutoff.
     */
    vec3 outAA = vec3(0);
    vec3 outDD = vec3(0);
    vec3 outSS = vec3(0);
    lightingColourPoint(p, n, i, outAA, outDD, outSS);
    float visibility = cutoff * shadowVisibility(shadow_p, shadow_n, i);
    outA += outAA * visibility;
    outD += outDD * visibility;
    outS += outSS * visibility;
    return;
}

/*
 * Lighting accumulation function. Does A/D/S separately.
 *
 * NB: For regular (i.e. non-radial/non-linear-falloff) ambient lights, if there's no global contribution
 *     then we need to apply the ambient constant (ka) flat to the diffuse colour.
 *     - See softrend/light24.c, SurfaceColourLit(), use_ambient_colour
 *     - See softrend/setup.c, ActiveLightsUpdate(), use_ambient_colour
 */
void accumulateLights(in vec3 position, in vec3 normal, in vec3 shadow_position, in vec3 shadow_normal,
                      inout vec3 ambient, inout vec3 diffuse, inout vec3 specular)
{
    if(!lighting) {
        return;
    }

#if !DEBUG_DISABLE_LIGHT_AMBIENT
    /*
     * If no non-radial ambient contributions, apply ka flat.
     * See above note.
     */
    if(use_ambient_colour) {
        diffuse += ka * ambient_colour.xyz;
    } else {
        diffuse += ka;
    }

    for(uint i = light_start.x; i < light_end.x; ++i) {
        lightingColourAmbientRadii(position, normal, i, ambient, diffuse, specular);
    }
#endif

#if !DEBUG_DISABLE_LIGHT_DIRECTIONAL
    for(uint i = light_start.y; i < light_end.y; ++i) {
        lightingColourDirect(position, normal, i, ambient, diffuse, specular);
    }
#endif

#if !DEBUG_DISABLE_LIGHT_POINT
    for(uint i = light_start.z; i < light_end.z; ++i) {
        lightingColourPoint(position, normal, i, ambient, diffuse, specular);
    }
#endif

#if !DEBUG_DISABLE_LIGHT_SPOT
    for(uint i = light_start.w; i < light_end.w; ++i) {
        lightingColourSpot(position, normal, shadow_position, shadow_normal, i, ambient, diffuse, specular);
    }
#endif
}
