/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: lighting.h 1.1 1997/12/10 16:52:13 jon Exp $
 * $Locker: $
 *
 * Geometry format
 */
#ifndef _GLIGHTING_H_
#define _GLIGHTING_H_

#ifdef __cplusplus
extern "C" {
#endif

/* BRender:
 * Private state of geometry format
 */
typedef struct br_geometry_lighting {
    /* BRender:
     * Dispatch table
     */
    struct br_geometry_lighting_dispatch *dispatch;

    /* BRender:
     * Standard object identifier
     */
    char *identifier;

    /* BRender:
     * Pointer to owning device
     */
    br_device *device;

    /* BRender:
     * Renderer type this format is associated with
     */
    br_renderer_facility *renderer_facility;

    /* BRenderModern:
     * Object query templates.
     */
    br_tv_template *templates;
} br_geometry_lighting;

#ifdef __cplusplus
};
#endif
#endif
