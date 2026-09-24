/* BRenderModern:
 * Private device CLUT state
 */
#ifndef _DEVCLUT_H_
#define _DEVCLUT_H_

#ifdef __cplusplus
extern "C" {
#endif

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

    br_boolean owned;

    SDL_Palette *pal;
} br_device_clut;

#ifdef __cplusplus
};
#endif
#endif
