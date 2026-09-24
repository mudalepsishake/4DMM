/* BRenderModern:
 * Private output facility structure
 */
#ifndef _OUTFCTY_H_
#define _OUTFCTY_H_

#ifdef __cplusplus
extern "C" {
#endif

/* BRenderModern:
 * Private state of output type
 */

typedef struct br_output_facility {
    /* BRenderModern:
     * Dispatch table
     */
    const struct br_output_facility_dispatch *dispatch;

    /* BRenderModern:
     * Standard object identifier
     */
    char *identifier;

    /* BRenderModern:
     * Pointer to owning device
     */
    struct br_device *device;

    /* BRenderModern:
     * List of instances associated with type
     */
    void *object_list;

    /* BRenderModern: Size of mode in pixels
     */
    br_int_32 width;
    br_int_32 height;

    /* BRenderModern:
     * Bit depth
     */
    br_int_32 colour_bits;

    /* BRenderModern:
     * Pixelmap types
     */
    br_uint_8 colour_type;

    /* BRenderModern:
     * Monitor index
     */
    br_int_32 monitor;
} br_output_facility;

#define OutputFacilityVGAType(c) (((br_output_facility *)c)->colour_type)

#ifdef __cplusplus
};
#endif
#endif
