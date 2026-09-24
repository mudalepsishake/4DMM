/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: devpixmp.h 1.1 1997/12/10 16:53:40 jon Exp $
 * $Locker: $
 *
 * Private device pixelmap structure
 */
#ifndef _DEVPIXMP_H_
#define _DEVPIXMP_H_

#ifdef __cplusplus
extern "C" {
#endif

/* BRender:
 * Private state of device pixelmap
 */
typedef struct br_device_pixelmap {
    /* BRender:
     * Dispatch table
     */
    struct br_device_pixelmap_dispatch *dispatch;

    /* BRender:
     * Standard handle identifier
     */
    char *pm_identifier;

    /** BRender: Standard pixelmap members (not including identifier) **/

    BR_PIXELMAP_MEMBERS

    /** BRender: End of br_pixelmap fields **/

    /* BRenderModern:
     * Pointer to owning device
     */
    struct br_device *device;

    struct br_output_facility *output_facility;
    struct br_device_clut     *clut;

    br_boolean restore_mode;
    br_uint_16 original_mode;

} br_device_pixelmap;

#define DevicePixelmapMemAddress(pm, x, y, bpp)                                                                                                         \
    ((char *)(((br_device_pixelmap *)(pm))->pm_pixels) + (((br_device_pixelmap *)(pm))->pm_base_y + (y)) * ((br_device_pixelmap *)(pm))->pm_row_bytes + \
     (((br_device_pixelmap *)(pm))->pm_base_x + (x)) * (bpp))

#ifdef __cplusplus
};
#endif
#endif
