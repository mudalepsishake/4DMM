/* BRenderModern:
 * Geometry format
 */
#ifndef _GV1MODEL_H_
#define _GV1MODEL_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BR_GEOMETRY_V1_MODEL_PRIVATE

/* BRenderModern:
 * Private state of geometry format
 */
typedef struct br_geometry_v1_model {
    /* BRenderModern:
     * Dispatch table
     */
    const struct br_geometry_v1_model_dispatch *dispatch;

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

} br_geometry_v1_model;

#endif

#ifdef __cplusplus
};
#endif
#endif
