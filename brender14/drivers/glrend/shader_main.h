#ifndef SHADER_MAIN_H_
#define SHADER_MAIN_H_

/*
 * v225: this is a REQUEST, not an allocation promise. Context initialisation
 * clamps it to GL_MAX_TEXTURE_SIZE and falls back by powers of two until both
 * shadow textures and the framebuffer really allocate successfully.
 *
 * 262144 is the nearest power-of-two to the requested ~256K experiment. No
 * normal GPU is expected to allocate one conventional map this large; keeping
 * the request high makes the runtime capability/fallback path authoritative.
 */
#define BR_GLREND_SHADOW_REQUESTED_SIZE 262144
#define BR_GLREND_SHADOW_MIN_SIZE 1024

/* Four quantisation steps in a 24-bit depth texture. */
#define BR_GLREND_SHADOW_DEPTH24_BIAS (4.0f / 16777216.0f)

typedef struct br_gl_main_shader {
    GLuint program;

    struct {
        GLint aPosition; /* BRenderModern: Vectex Position, vec3 */
        GLint aUV;       /* BRenderModern: UV, vec2 */
        GLint aNormal;   /* BRenderModern: Vertex Normal, vec3 */
        GLint aColour;   /* BRenderModern: Vertex colour, vec4 */
    } attributes;

    struct {
        GLint main_texture;         /* BRenderModern: sampler2D */
        GLint index_texture;        /* BRenderModern: usampler2D */
        GLint shadow_texture;         /* sampler2D caster depth */
        GLint shadow_owner_texture;   /* usampler2D owner id */
        GLint shadow_blocker_texture; /* sampler2D no-cast stop depth */
        GLint shadow_detail_texture;  /* sampler2D nested detail depth */
    } uniforms;

    GLuint ubo_scene;
    GLuint block_index_scene;
    GLuint block_binding_scene;

    GLuint block_index_model;
    GLuint block_binding_model;

    GLint main_texture_binding;
    GLint index_texture_binding;
    GLint shadow_texture_binding;
    GLint shadow_owner_texture_binding;
    GLint shadow_blocker_texture_binding;
    GLint shadow_detail_texture_binding;
} br_gl_main_shader;

typedef struct br_gl_shadow_shader {
    GLuint program;
    GLuint block_index_scene;
    GLuint block_binding_scene;
    GLuint block_index_model;
    GLuint block_binding_model;
    GLint uniform_base_depth_texture;
    GLint uniform_layer_mode;
    GLint base_depth_texture_binding;
} br_gl_shadow_shader;

#pragma pack(push, 16)
typedef struct br_gl_main_data_light_info {
    alignas(4) uint32_t type;
    alignas(4) uint32_t attenuation_type;
    alignas(4) uint32_t _pad0;
    alignas(4) uint32_t _pad1;
} br_gl_main_data_light_info;
BR_STATIC_ASSERT(sizeof(br_gl_main_data_light_info) == sizeof(br_vector4_f), "sizeof(br_gl_main_data_light_info) != sizeof(br_vector4_f)");

typedef struct br_gl_main_data_light_atten {
    alignas(4) br_float intensity;
    alignas(4) br_float attenuation_c;
    alignas(4) br_float attenuation_l;
    alignas(4) br_float attenuation_q;
} br_gl_main_data_light_atten;
BR_STATIC_ASSERT(sizeof(br_gl_main_data_light_atten) == sizeof(br_vector4_f), "sizeof(br_gl_main_data_light_atten) != sizeof(br_vector4_f)");

typedef struct br_gl_main_data_light_radii {
    alignas(4) br_float spot_cos_inner;
    alignas(4) br_float spot_cos_outer;
    alignas(4) br_float radius_inner;
    alignas(4) br_float radius_outer;
} br_gl_main_data_light_radii;
BR_STATIC_ASSERT(sizeof(br_gl_main_data_light_radii) == sizeof(br_vector4_f), "sizeof(br_gl_main_data_light_radii) != sizeof(br_vector4_f)");

typedef struct br_gl_main_data_scene {
    alignas(16) br_vector4_f eye_view;
    alignas(16) br_gl_main_data_light_info light_info[BR_MAX_LIGHTS];   /* BRenderModern: (type, atten_type, 0, 0) */
    alignas(16) br_vector4_f light_positions[BR_MAX_LIGHTS];              /* BRenderModern: (X, Y, Z, 0) */
    alignas(16) br_vector4_f light_directions[BR_MAX_LIGHTS];             /* BRenderModern: (X, Y, Z, 0), normalised */
    alignas(16) br_vector4_f light_halfs[BR_MAX_LIGHTS];                  /* BRenderModern: (X, Y, Z, 0), normalised */
    alignas(16) br_vector4_f light_colours[BR_MAX_LIGHTS];                /* BRenderModern: (R, G, B, 0)   */
    alignas(16) br_gl_main_data_light_atten light_atten[BR_MAX_LIGHTS]; /* BRenderModern: (1/C, C, L, Q) */
    alignas(16) br_gl_main_data_light_radii light_radii[BR_MAX_LIGHTS]; /* BRenderModern: (cos(inner), cos(outer), radius_inner, radius_outer) */
    alignas(16) br_vector4_f clip_planes[BR_MAX_CLIP_PLANES];
    alignas(16) br_vector4_f ambient_colour;
    alignas(16) br_vector4_i light_start;
    alignas(16) br_vector4_i light_end;
    alignas(4) uint32_t num_clip_planes;
    alignas(4) uint32_t use_ambient_colour;
    alignas(16) br_vector4_f shadow_info;  /* enabled, light index, tan(half-FOV), near */
    alignas(16) br_vector4_f shadow_info2; /* far, depth bias, reserved, reserved */
    alignas(16) br_vector4_f shadow_world_up_view;    /* world +Y transformed into view space */
    alignas(16) br_vector4_f shadow_world_right_view; /* world +X transformed into view space */
    alignas(16) br_vector4_f shadow_detail_fit;       /* center_u, center_v, half_u, half_v */
    alignas(16) br_vector4_f shadow_detail_info;      /* enabled, render_detail_pass, zoom, reserved */
} br_gl_main_data_scene;

typedef struct br_gl_main_data_model {
    alignas(16) br_matrix4_f model_view;
    alignas(16) br_matrix4_f projection;
    alignas(16) br_matrix4_f mvp;
    alignas(16) br_matrix4_f shadow_model_to_light;
    alignas(16) br_matrix4_f normal_matrix;
    alignas(16) br_matrix4_f environment_matrix;
    alignas(16) br_matrix4_f map_transform;
    alignas(16) br_vector4_f surface_colour;
    alignas(16) br_vector4_f eye_m;
    alignas(16) br_vector4_f fog_colour;
    alignas(8) br_vector2_f fog_range;
    alignas(4) float ka;
    alignas(4) float ks;
    alignas(4) float kd;
    alignas(4) float power;
    alignas(4) uint32_t lighting;
    alignas(4) uint32_t prelighting;
    alignas(4) uint32_t colour_source;
    alignas(4) uint32_t uv_source;
    alignas(4) uint32_t disable_colour_key;
    alignas(4) uint32_t texture_mode;
    alignas(4) uint32_t enable_fog;
    alignas(4) br_float fog_scale;
    alignas(4) uint32_t shading_mode;
    alignas(16) br_vector4_i shadow_owner_info; /* x = logical 3DMM object owner */
} br_gl_main_data_model;
#pragma pack(pop)

br_boolean ShaderGLShadowCompile(br_gl_shadow_shader *self, const GladGLContext *gl);

#endif /* BRenderModern: SHADER_MAIN_H_ */
