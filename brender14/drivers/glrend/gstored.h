/* BRenderModern:
 * Geometry format
 */
#ifndef _GSTORED_H_
#define _GSTORED_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gl_vertex_f {
    /* BRenderModern:
     * Position
     */
    br_vector3_f p;

    /* BRenderModern:
     * Mapping coordinates
     */
    br_vector2_f map;

    /* BRenderModern:
     * Surface normal at vertex
     */
    br_vector3_f n;

    /* BRenderModern:
     * Colour
     */
    br_uint_8 c[4];
} gl_vertex_f;

typedef struct gl_groupinfo {
    /* BRenderModern:
     * Count for glDrawElements(). Use GL_TRIANGLES.
     */
    GLsizei count;

    /*
     * Offset for triangle glDrawElements().
     */
    void *offset;

    /*
     * Unique prepared-edge indices for BRT_LINE.  BRender edge id 0 marks
     * internal/coplanar edges and is deliberately omitted.
     */
    GLsizei line_count;
    void   *line_offset;

    /* BRenderModern:
     * Byte offset of this group's first vertex in the VBO.
     */
    GLsizei vertex_offset;

    /* BRenderModern:
     * The group itself
     */
    struct v11group *group;
} gl_groupinfo;

#ifdef BR_GEOMETRY_STORED_PRIVATE

/* BRenderModern:
 * Private state of geometry format
 */
typedef struct br_geometry_stored {
    /* BRenderModern:
     * Dispatch table
     */
    const struct br_geometry_stored_dispatch *dispatch;

    /* BRenderModern:
     * Standard object identifier
     */
    const char *identifier;

    /* BRenderModern:
     * Pointer to owning device
     */
    struct br_device *device;

    struct br_geometry_v1_model *gv1model;

    br_boolean       shared;
    struct v11model *model;

    /* BRenderModern:
     * GL Dispatch
     */
    const GladGLContext *gl;

    GLuint gl_vao;
    GLuint gl_vbo;
    GLuint gl_ibo;

    gl_groupinfo *groups;
} br_geometry_stored;

#endif

#ifdef __cplusplus
};
#endif
#endif /* BRenderModern: _GSTORED_H_ */
