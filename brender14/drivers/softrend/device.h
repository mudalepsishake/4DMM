/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: device.h 1.1 1997/12/10 16:51:43 jon Exp $
 * $Locker: $
 *
 * Private device driver structure
 */
#ifndef _DEVICE_H_
#define _DEVICE_H_

#include "host.h"

#ifdef __cplusplus
extern "C" {
#endif
/* BRender:
 * Private state of device
 */
typedef struct br_device {
    /* BRender:
     * Dispatch table
     */
    struct br_device_dispatch *dispatch;

    /* BRender:
     * Standard object identifier
     */
    char *identifier;

    /* BRender:
     * Pointer to owning device
     */
    struct br_device *device;

    /* BRender:
     * List of objects associated with this device
     */
    void *object_list;

    /* BRender:
     * Anchor for all device's resources
     */
    void *res;

    /* BRenderModern:
     * Object query templates.
     */
    br_tv_template *templates;

    // BRender: local copy of cpu capabilities
    host_info hostInfo;

} br_device;

/* BRender:
 * Some useful inline ops.
 */
#define DeviceSoftResource(d) (((br_device *)d)->res)

#ifdef __cplusplus
};
#endif
#endif
