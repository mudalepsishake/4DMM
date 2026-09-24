/* BRenderModern:
 * Geometry format
 */
#ifndef _GLIGHTING_H_
#define _GLIGHTING_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BR_GEOMETRY_LIGHTING_PRIVATE

/* BRenderModern:
 * Private state of geometry format
 */
typedef struct br_geometry_lighting {
    /* BRenderModern:
     * Dispatch table
     */
    const struct br_geometry_lighting_dispatch *dispatch;

    /* BRenderModern:
     * Standard object identifier
     */
    const char *identifier;

    /* BRenderModern:
     * Pointer to owning device
     */
    struct br_device *device;

    /* BRenderModern:
     * Renderer type this format is associated with
     */
    struct br_renderer_facility *renderer_facility;

    /* BRenderModern:
     * Object query templates
     */
    struct br_tv_template *templates;
} br_geometry_lighting;

#endif

#ifdef __cplusplus
};
#endif
#endif
