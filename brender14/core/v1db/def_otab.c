/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: def_otab.c 1.1 1997/12/10 16:41:30 jon Exp $
 * $Locker: $
 *
 * Default order_table for bucket Z-sort renderer
 */
#include "v1db.h"

#define DEFAULT_ORDER_TABLE_SIZE 256

static br_primitive *default_table[DEFAULT_ORDER_TABLE_SIZE];

br_order_table _BrDefaultOrderTable = {
    /* BRender:
     * Default order table
     */
    .table  = default_table,               /* BRender: Pointer to table */
    .size   = DEFAULT_ORDER_TABLE_SIZE,    /* BRender: Size             */
    .next   = NULL,                        /* BRender: Next             */
    .min_z  = BR_SCALAR(1.0),              /* BRender: Minimum Z        */
    .max_z  = BR_SCALAR(10.0),             /* BRender: Maximum Z        */
    .sort_z = BR_SCALAR(0.0),              /* BRender: Sort Z           */
    .scale  = BR_SCALAR(1.0 / 9.0),        /* BRender: Scale            */
    .flags  = BR_ORDER_TABLE_LEAVE_BOUNDS, /* BRender: Flags            */
    .type   = BR_SORT_AVERAGE,             /* BRender: Sort type        */
    .visits = 0                            /* BRender: Visit count      */
};
