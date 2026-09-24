/* BRenderModern:
 * Private device CLUT state
 */
#ifndef _DEVCLUT_H_
#define _DEVCLUT_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BR_DEVICE_CLUT_PRIVATE
/* BRenderModern:
 * Private state of device CLUT
 */
typedef struct br_device_clut {
    /* BRenderModern:
     * Dispatch table
     */
    const struct br_device_clut_dispatch *dispatch;

    /* BRenderModern:
     * Standard handle identifier
     */
    const char *identifier;

    /* BRenderModern:
     * Device pointer
     */
    br_device *device;

    /* BRenderModern:
     * Internal texture, used to make our lives easier.
     */
    br_pixelmap *pm;

    /* BRenderModern:
     * Storage.
     */
    br_colour entries[BR_GLREND_MAX_CLUT_ENTRIES];

    /* BRenderModern:
     * GL Dispatch.
     */
    const GladGLContext *gl;

    /* BRenderModern:
     * OpenGL texture handle.
     */
    GLuint gl_tex;
} br_device_clut;

#endif

#ifdef __cplusplus
};
#endif
#endif
