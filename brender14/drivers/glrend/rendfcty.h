/* BRenderModern:
 * Private renderer facility structure
 */
#ifndef _RENDFCTY_H_
#define _RENDFCTY_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BR_RENDERER_FACILITY_PRIVATE

/* BRenderModern:
 * Private state of renderer facility
 */
typedef struct br_renderer_facility {
    /* BRenderModern:
     * Dispatch table
     */
    const struct br_renderer_facility_dispatch *dispatch;

    /* BRenderModern:
     * Standard object identifier
     */
    const char *identifier;

    /* BRenderModern:
     * Pointer to owning device
     */
    struct br_device *device;

    /* BRenderModern:
     * List of objects associated with this device
     */
    void *object_list;

} br_renderer_facility;

#endif

#ifdef __cplusplus
};
#endif
#endif
