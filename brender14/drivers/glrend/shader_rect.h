#ifndef SHADER_RECT_H_
#define SHADER_RECT_H_

#pragma pack(push, 16)
typedef struct br_gl_rect_data {
    alignas(16) br_matrix4_f mvp;
    alignas(16) br_vector4_f src_rect;
    alignas(16) br_vector4_f dst_rect;
    alignas(4) float vertical_flip;
    alignas(4) float indexed;
} br_gl_rect_data;
#pragma pack(pop)

typedef struct br_gl_rect_shader {
    GLuint program;
    GLint  uSampler;  /* BRenderModern: Sampler, sampler2D */
    GLint  uIndexTex; /* BRenderModern: Sampler, usampler2D */

    GLuint block_index_rect_data;
    GLuint block_binding_rect_data;

    GLuint vao;
    GLuint ubo;
} br_gl_rect_shader;

#endif /* BRenderModern: SHADER_RECT_H_ */
