#ifndef _DEVICE_H_
#define _DEVICE_H_

/* BRenderModern:
 * Private state of device
 */
typedef struct br_device {
    /* BRenderModern:
     * Dispatch table
     */
    const struct br_device_dispatch *dispatch;

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

    /* BRenderModern:
     * Anchor for all device's resources
     */
    void *res;

    /* BRenderModern:
     * Driver-wide template store
     */
    struct device_templates templates;

    /* BRenderModern:
     * Device-wide output facility.
     */
    struct br_output_facility *output_facility;
} br_device;

#define DeviceGLResource(d) (((br_device *)(d))->res)

#endif /* BRenderModern: _DEVICE_H_ */
