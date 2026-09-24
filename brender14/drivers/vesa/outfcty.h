/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: outfcty.h 1.1 1997/12/10 16:54:18 jon Exp $
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
 * Private state of output facility
 */
typedef struct br_output_facility {
    /* BRender:
     * Dispatch table
     */
    struct br_output_facility_dispatch *dispatch;

    /* BRender:
     * Standard object identifier
     */
    char *identifier;

    /* BRenderModern:
     * Pointer to owning device
     */
    br_device *device;

    /* BRender:
     * List of instances associated with facility
     */
    void *object_list;

    /* BRender: Size of mode in pixels
     */
    br_int_32 width;
    br_int_32 height;

    /* BRender: Bit depth
     */
    br_int_32 colour_bits;
    br_int_32 depth_bits;

    /* BRender: Pixelmap types
     */
    br_int_32 colour_type;
    br_int_32 depth_type;

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

    /* BRender:
     * VESA mode for this
     */
    br_uint_16 vesa_mode;

    /* BRender:
     * VESA mode info for this
     */
    struct vesa_modeinfo modeinfo;

} br_output_facility;

#define OutputFacilityVESAType(c)     (((br_output_facility *)c)->colour_type)
#define OutputFacilityVESABits(c)     (((br_output_facility *)c)->colour_bits)

#define OutputFacilityVESAMode(c)     (((br_output_facility *)c)->vesa_mode)
#define OutputFacilityVESAModeInfo(c) (&(((br_output_facility *)c)->modeinfo))

#ifdef __cplusplus
};
#endif
#endif
