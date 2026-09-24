/* BRender:
 * Copyright (c) 1993-1995 by Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: order.h 1.1 1997/12/10 16:41:18 jon Exp $
 * $Locker: $
 *
 * Definitions for order tables and primitives
 */
#ifndef _ORDER_H_
#define _ORDER_H_

/* BRender:
 * Order table definitions
 */
enum {
    BR_ORDER_TABLE_LEAVE_BOUNDS = 0x0000,
    BR_ORDER_TABLE_NEW_BOUNDS   = 0x0001,
    BR_ORDER_TABLE_INIT_BOUNDS  = 0x0002,

    BR_ORDER_TABLE_CONVEX = 0x0004,

    BR_ORDER_TABLE_SORT_NEAR   = 0x0008,
    BR_ORDER_TABLE_SORT_FAR    = 0x0010,
    BR_ORDER_TABLE_SORT_CENTRE = 0x0020,

    /* BRenderModern:
     * Primitives will be sorted within buckets
     * upon insertion.
     */
    BR_ORDER_TABLE_BUCKET_SORT = 0x0040,
};

/* BRender:
 * Sort types
 */
enum {
    BR_SORT_AVERAGE      = 0x0001,
    BR_SORT_MIN          = 0x0002,
    BR_SORT_MAX          = 0x0003,
    BR_SORT_FIRST_VERTEX = 0x0004
};

/* BRender:
 * Primitive types
 */
enum {
    BR_PRIMITIVE_POINT    = BRT_POINT,
    BR_PRIMITIVE_LINE     = BRT_LINE,
    BR_PRIMITIVE_TRIANGLE = BRT_TRIANGLE
};

/* BRender:
 * The primitive structure
 */
typedef struct br_primitive {

    /* BRender:
     * Next primitive in list
     */
    struct br_primitive *next;

    /* BRenderModern:
     * Cached calculated depth of the primitive.
     * Some devices may use this for sorting within the bucket.
     */
    br_scalar depth;

    /* BRender:
     * Primitive type
     */
    br_uint_32 type;

    /* BRender:
     * Properties
     */
    void *stored;

    /* BRender:
     * Pointers to vertex data
     */
    void *v[3];

} br_primitive;

/* BRender:
 * The order table structure
 */
typedef struct br_order_table {

    /* BRender:
     * Order table array
     */
    br_primitive **table;

    /* BRender:
     * Number of entries in array
     */
    br_uint_16 size;

    /* BRender:
     * Next order table in list
     */
    struct br_order_table *next;

    /* BRender:
     * Order table bounds
     */
    br_scalar min_z;
    br_scalar max_z;

    /* BRender:
     * Order table sort value
     */
    br_scalar sort_z;

    /* BRender:
     * Scale for sorted depths
     */
    br_scalar scale;

    /* BRender:
     * Flags
     */
    br_uint_32 flags;

    /* BRender:
     * Sort type
     */
    br_uint_16 type;

    /* BRender:
     * Visit counter
     */
    br_uint_16 visits;

} br_order_table;

/* BRender:
 * workspace for storing primitives
 */
typedef struct br_primitive_heap {
    br_uint_8 *base;
    br_size_t  size;
    br_uint_8 *current;
} br_primitive_heap;

/* BRender:
 * structure used by ZsOrderTableTraversal
 */
typedef struct {
    br_scalar screenX;
    br_scalar screenY;
    br_scalar distanceZ;
} ot_vertex;

#endif
