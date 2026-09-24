#ifndef SHADER_LINE_H_
#define SHADER_LINE_H_

#pragma pack(push, 16)
typedef struct br_gl_line_data {
    alignas(16) br_matrix4_f mvp;
    alignas(8) br_vector2_f start;
    alignas(8) br_vector2_f end;
    alignas(16) br_vector4_f colour;
} br_gl_line_data;
#pragma pack(pop)

typedef struct br_gl_line_shader {
    GLuint program;
    GLuint block_index_line_data;
    GLuint block_binding_line_data;

    GLuint vao;
    GLuint ubo;
} br_gl_line_shader;

#endif /* BRenderModern: SHADER_LINE_H_ */
