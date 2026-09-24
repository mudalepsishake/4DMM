/* BRenderModern:
 * Stored buffer structure
 */
#ifndef _SBUFFER_H_
#define _SBUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

/* BRenderModern:
 * Block structure
 */
typedef struct br_buffer_stored {
    /* BRenderModern:
     * Dispatch table
     */
    struct br_buffer_stored_dispatch *dispatch;

    /* BRenderModern:
     * Standard object identifier
     */
    const char *identifier;

    br_device                   *device;
    struct br_primitive_library *plib;

    /* BRenderModern:
     * Source pixelmap
     */
    br_pixelmap *source;

    /* BRenderModern:
     * Copy of source flags
     */
    br_uint_16 source_flags;

    const GladGLContext *gl;

    /* BRenderModern:
     * OpenGL texture handle
     */
    GLuint gl_tex_nokey;
    GLuint gl_tex_keyed;

    const br_pixelmap_gl_fmt *fmt;

    /* BRenderModern:
     * Object query templates.
     * FIXME: These should be stored on the device, but that can't
     *  be done until everything's ported to C.
     */
    struct br_tv_template *templates;
} br_buffer_stored;

#ifdef __cplusplus
};
#endif
#endif
