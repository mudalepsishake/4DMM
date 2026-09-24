/* BRender:
 * Copyright (c) 1992,1993-1995 by Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: vector.h 1.9 1995/06/30 16:09:07 sam Exp $
 * $Locker:  $
 *
 * Vector types
 */

#ifndef _VECTOR_H_
#define _VECTOR_H_

#ifdef __cplusplus
extern "C"
{
#endif

    /* BRender:
     * Vector types
     */
    typedef struct br_vector2
    {
        br_scalar v[2];
    } br_vector2;

    typedef struct br_vector3
    {
        br_scalar v[3];
    } br_vector3;

    typedef struct br_vector4
    {
        br_scalar v[4];
    } br_vector4;

    /* BRender:
     * Integer vectors (used for integer bounding boxes)
     */
    typedef struct br_vector2i
    {
        br_int_32 v[2];
    } br_vector2i;

    typedef struct br_vector3i
    {
        br_int_32 v[3];
    } br_vector3i;

    typedef struct br_vector4i
    {
        br_int_32 v[4];
    } br_vector4i;

    /* BRender:
     * Fraction vector types
     */
    typedef struct br_fvector2
    {
        br_fraction v[2];
    } br_fvector2;

    typedef struct br_fvector3
    {
        br_fraction v[3];
    } br_fvector3;

    typedef struct br_fvector4
    {
        br_fraction v[4];
    } br_fvector4;

    /* BRender:
     * General structures for describing an axis aligned bounds
     */
    typedef struct br_bounds2
    {
        br_vector2 min; /* BRender: Minimum corner */
        br_vector2 max; /* BRender: Maximum corner */
    } br_bounds2;

    typedef struct br_bounds3
    {
        br_vector3 min; /* BRender: Minimum corner */
        br_vector3 max; /* BRender: Maximum corner */
    } br_bounds3;

    typedef struct br_bounds4
    {
        br_vector4 min; /* BRender: Minimum corner */
        br_vector4 max; /* BRender: Maximum corner */
    } br_bounds4;

    /* BRender:
     * Integer bounding boxes
     */
    typedef struct br_bounds2i
    {
        br_vector2i min; /* BRender: Minimum corner */
        br_vector2i max; /* BRender: Maximum corner */
    } br_bounds2i;

    typedef struct br_bounds3i
    {
        br_vector3i min; /* BRender: Minimum corner */
        br_vector3i max; /* BRender: Maximum corner */
    } br_bounds3i;

    typedef struct br_bounds4i
    {
        br_vector4i min; /* BRender: Minimum corner */
        br_vector4i max; /* BRender: Maximum corner */
    } br_bounds4i;

    /* BRender:
     * Backwards compatibility
     */
    typedef struct br_bounds3 br_bounds;

/* BRender:
 * Macros for static initialisation
 */
#define BR_VECTOR2(a, b)                                                                                               \
    {                                                                                                                  \
        BR_SCALAR(a), BR_SCALAR(b)                                                                                     \
    }
#define BR_VECTOR3(a, b, c)                                                                                            \
    {                                                                                                                  \
        BR_SCALAR(a), BR_SCALAR(b), BR_SCALAR(c)                                                                       \
    }
#define BR_VECTOR4(a, b, c, d)                                                                                         \
    {                                                                                                                  \
        BR_SCALAR(a), BR_SCALAR(b), BR_SCALAR(c), BR_SCALAR(d)                                                         \
    }

#define BR_FVECTOR2(a, b)                                                                                              \
    {                                                                                                                  \
        BR_FRACTION(a), BR_FRACTION(b)                                                                                 \
    }
#define BR_FVECTOR3(a, b, c)                                                                                           \
    {                                                                                                                  \
        BR_FRACTION(a), BR_FRACTION(b), BR_FRACTION(c)                                                                 \
    }
#define BR_FVECTOR4(a, b, c, d)                                                                                        \
    {                                                                                                                  \
        BR_FRACTION(a), BR_FRACTION(b), BR_FRACTION(c), BR_FRACTION(d)                                                 \
    }

#ifdef __cplusplus
};
#endif
#endif
