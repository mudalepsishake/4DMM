/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: outfcty.h 1.1 1997/12/10 16:45:47 jon Exp $
 * $Locker: $
 *
 * Private output facility structure
 */
#ifndef _OUTFCTY_H_
#define _OUTFCTY_H_

#ifdef __cplusplus
extern "C" {
#endif

/* BRender:
 * Private state of output type
 */

typedef struct br_output_facility {
    /* BRender:
     * Dispatch table
     */
    const struct br_output_facility_dispatch *dispatch;

    /* BRender:
     * Standard object identifier
     */
    const char *identifier;

    /* BRender:
     * Pointer to owning device
     */
    struct br_device *device;

    /* BRender:
     * List of instances associated with type
     */
    void *object_list;

    /* BRender:
     * BIOS mode for INT 10
     */
    br_int_32 bios_mode;

    /* BRender: Size of mode in pixels
     */
    br_int_32 width;
    br_int_32 height;

    /* BRender: Bit depth
     */
    br_int_32 colour_bits;
    // BRender:	br_int_32	depth_bits;

    /* BRender: Pixelmap types
     */
    br_int_32 colour_type;
    // BRender:	br_int_32	depth_type;

    /* BRender: Is there a CLUT?
     */
    br_boolean indexed;

    /* BRender: Video memory size
     */
    br_int_32 video_memory;
    br_int_32 host_memory;

    /* BRender:
     * Number of instances
     */
    br_int_32 num_instances;

    /* BRender: Default CLUT
     */
    struct br_device_clut *default_clut;

} br_output_facility;

#define OutputFacilityVGAType(c) (((br_output_facility *)c)->colour_type)

#define STATIC_OUTPUT_FACILITY_VGA(id, w, h, cb, db, ct, dt, idx, vm, hm) \
    {                                                                     \
        NULL, id, NULL, w, h, cb, db, ct, dt, idx, vm, hm,                \
    }

#ifdef __cplusplus
};
#endif
#endif
