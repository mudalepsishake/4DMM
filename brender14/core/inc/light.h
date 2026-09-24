/* BRender:
 * Copyright (c) 1993-1995 by Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: light.h 1.3 1998/06/17 16:11:46 jon Exp $
 * $Locker: $
 *
 * Definitons for a light
 */
#ifndef _LIGHT_H_
#define _LIGHT_H_

enum {
    /* BRender:
     * Type of light
     */
    BR_LIGHT_TYPE    = 0x0003,
    BR_LIGHT_POINT   = 0x0000,
    BR_LIGHT_DIRECT  = 0x0001,
    BR_LIGHT_SPOT    = 0x0002,
    BR_LIGHT_AMBIENT = 0x0003,

    /* BRender:
     * Flag indicating that calculations are done in view space
     */
    BR_LIGHT_VIEW = 0x0004,

    /* BRender:
     * Flag indicating that linear falloff should be used
     */
    BR_LIGHT_LINEAR_FALLOFF = 0x0008,

    /*
     * 4DMM/BRender 1.4: mark the single Light Lab light that owns the
     * optional shadow-map pass. The ordinary light type remains selected by
     * BR_LIGHT_TYPE, so legacy renderers simply ignore this extra flag.
     */
    BR_LIGHT_SHADOW = 0x0010,
};

/* BRender:
 * Definition of cutoff volumes - vertex is lit if it falls within any of
 * a number of convex volumes, and partially lit if it falls within the
 * falloff distance from any
 */
typedef struct br_light_volume {

    br_scalar         falloff_distance;
    br_convex_region *regions;
    br_uint_32        nregions;

} br_light_volume;

typedef struct br_light {
    /* BRender:
     * Optional identifier
     */
    char *identifier;

    /* BRender:
     * Type of light
     */
    br_uint_8 type;

    /* BRender:
     * Colour of light (if renderer supports it)
     */
    br_colour colour;

    /* BRender:
     * Attenuation of light with distance - constant, linear, and quadratic
     * l & q only apply to point and spot lights
     */
    br_scalar attenuation_c;
    br_scalar attenuation_l;
    br_scalar attenuation_q;

    /* BRenderModern:
     * Cone angles for spotlights.
     *
     * Will be clamped as follows:
     *  cone_inner ≤ cone_outer ≤ 0.5 (180°, π rad)
     *
     *        Full Brightness Zone
     *      /----------------------\     (cone_inner)
     *     /      Smooth Fade       \
     *    /--------------------------\   (cone_outer)
     *   /        No Light (0%)       \
     *  /______________________________\
     */
    br_angle cone_outer;
    br_angle cone_inner;

    /* BRender:
     * Sphere radii for linear falloff and cutoff of normally attenuated lights
     */
    br_scalar radius_outer;
    br_scalar radius_inner;

    /* BRender:
     * Cutoff volumes
     */
    br_light_volume volume;
    void           *user;

} br_light;

#endif
