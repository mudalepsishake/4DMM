/* BRenderModern:
 * Private device pixelmap structure
 */
#ifndef _DEVPIXMP_H_
#define _DEVPIXMP_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pixelmap_new_tokens {
    const char              *title;
    br_int_32                width;
    br_int_32                height;
    br_int_32                pixel_bits;
    br_uint_8                pixel_type;
    SDL_Window              *window;
    SDL_Surface             *surface;
    br_token                 use_type;
    br_uint_32               flags;
    br_boolean               fullscreen;
    br_device_sdl_ext_procs *ext_procs;
} pixelmap_new_tokens;

/* BRender:
 * Private state of device pixelmap
 */
typedef struct br_device_pixelmap {
    /* BRender:
     * Dispatch table
     */
    const struct br_device_pixelmap_dispatch *dispatch;

    /* BRender:
     * Standard handle identifier
     */
    const char *pm_identifier;

    /** BRender: Standard pixelmap members (not including identifier**/

    BR_PIXELMAP_MEMBERS_PREFIXED(pm_)

    /** BRender: End of br_pixelmap fields **/

    /* BRender:
     * Pointer to owning device - special case - has to be after public fields
     * because of backwards compatibility for the pixelmaps structure
     */
    struct br_device *device;

    struct br_output_facility *output_facility;

    br_boolean owned;

    /* BRenderModern:
     * Our backing SDL surface.
     */
    SDL_Surface *surface;

    /* BRenderModern:
     * SDL rendererer, used for line and point drawing.
     */
    SDL_Renderer *renderer;

    /* BRenderModern:
     * The SDL window, if we were created for one.
     */
    SDL_Window *window;

    struct br_device_clut *clut;

    /* BRenderModern:
     * Application-specific external procedures (callbacks).
     */
    br_device_sdl_ext_procs ext_procs;
} br_device_pixelmap;

#ifdef __cplusplus
};
#endif
#endif
