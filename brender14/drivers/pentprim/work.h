/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: work.h 1.1 1997/12/10 16:49:22 jon Exp $
 * $Locker: $
 *
 * Private work area and parameters for primitives
 */
#ifndef _WORK_H_
#define _WORK_H_

#define SCREEN_FIXED 1

/* BRender:
 * Scan convertion details for one edge of triangle
 */
struct scan_edge {
	br_fixed_ls	x; 		/* BRender: delta x along edge				*/
	br_fixed_ls	y;	 	/* BRender: delta y along edge				*/
	br_fixed_ls	grad;	/* BRender: gradient (x/y)					*/
	br_int_32	f;		/* BRender: Starting value		(fraction)  */
	br_int_32	i;		/* BRender:						(integer)   */
	br_int_32	d_f;	/* BRender: delta_x per scanline	(fraction)  */
	br_int_32	d_i;	/* BRender:             			(integer)   */
	br_int_32   start;  /* BRender: starting scanline				*/
	br_int_32   count;  /* BRender: total scanlines				    */
};

/* BRender:
 * Scan convertion details for one parameter
 */
struct scan_parameter {
	br_fixed_ls	currentpix;	/* BRender: Parameter (16.16) value at pixel				*/
	br_fixed_ls	current;	/* BRender: Parameter (16.16) value at start of scanline	*/
	br_fixed_ls	d_carry;	/* BRender: Increment per scanline if carry from bit 15	*/
	br_fixed_ls	d_nocarry;	/* BRender:   ""              ""      no carry   ""    	*/
	br_fixed_ls	grad_x;		/* BRender: Gradient of parameter along X axis			*/
	br_fixed_ls	grad_y;		/* BRender: Gradient of parameter along Y axis			*/
};

/* BRender:
 * Arbitrary width scan line data
 */
struct arbitrary_width_scan {
    struct scan_edge *edge;
    char *start,*end;
    char *zstart;
    char *source_current;
    short u_int_current;
    short pad;
    unsigned u_current,du,du_carry,du_nocarry;
    int du_int,du_int_nocarry,du_int_carry;
    unsigned v_current,dv,dv_carry,dv_nocarry;
    int dsource,dsource_carry,dsource_nocarry;
};

/* BRender:
 * Perspective texture mapper data
 */
struct perspective_scan {
	char *start;
	char *dest;
 	char *end;
	char *zstart;
	char *zdest;
 	int i;
	int di;
	int z;
	int dz;
	int source;
	int direction;
	int denominator;
	int ddenominator;
	int u_numerator;
	int	v_numerator;
	int du_numerator;
	int dv_numerator;
	int du_numerator_nocarry;
	int dv_numerator_nocarry;
	int y;	/** BRender: *********/
};

/* BRender:
 * A minimal description of a pixelmap (with all origins and
 * bases factored out)
 */
struct render_buffer {
	void * base;			/* BRender: 0,0 pixel				*/
	br_uint_16 sel;			/* BRender: Optional segment			*/
	br_uint_8 type;			/* BRender: Original pixelmap type	*/
	br_uint_8 bpp;			/* BRender: Bytes per pixel			*/
	br_uint_32 width_b;		/* BRender: In bytes					*/
	br_uint_32 width_p;		/* BRender: In pixels				*/
	br_uint_32 height;		/* BRender: In scanlines				*/
        br_int_32 stride_b;    /* BRender: In bytes                                     */
        br_int_32 stride_p;    /* BRender: In pixels                            */
	br_uint_32 size;		/* BRender: stride * width_bytes 	*/

	br_uint_32 *palette;	/* BRender: Colour map, if valid 	*/
	br_int_32 palette_size; /* BRender: Number of entries in map */

	br_uint_8 width_s;		/* BRender: Power of 2 for width		*/
	br_uint_8 height_s;		/* BRender: Power of 2 for height	*/
	br_uint_8 tile_s;		/* BRender: Number of V bits to tile */
   	br_uint_8 _pad0;
};

/* BRender:
 * Static work area for renderers
 */
typedef struct prim_work {
	/* BRender:
	 * Buffers
	 */
	struct render_buffer colour;
	struct render_buffer bump;
	struct render_buffer depth;
	struct render_buffer texture;

	/* BRender:
	 * Lookup tables
	 */
	br_uint_8 *shade_table;
	br_uint_8 *blend_table;
	br_uint_32 *lighting_table;
	br_uint_32 *screendoor_table;
        br_uint_8 *fog_table;

	br_uint_8 shade_type;
	br_uint_8 blend_type;
	br_uint_8 lighting_type;
	br_uint_8 screendoor_type;
        br_uint_8 fog_type;

	/* BRender:
	 * Workspace for triangle scan converter
	 */
	struct scan_edge main;		/* BRender: Long edge of triangle				   */
	struct scan_edge top;		/* BRender: Top short edge						   */
	struct scan_edge bot;		/* BRender: Bottom short edge					   */

	struct scan_parameter pz;  	/* BRender: Depth				*/

	struct scan_parameter pu;	/* BRender: Mapping				*/
	struct scan_parameter pv;

	struct scan_parameter pi;	/* BRender: Index				*/

	struct scan_parameter pr;	/* BRender: Red					*/
	struct scan_parameter pg;	/* BRender: Green				*/
	struct scan_parameter pb;	/* BRender: Blue					*/

	struct scan_parameter pq;	/* BRender: Perspective divisor	*/

	struct scan_parameter source;	/* BRender: Current texel */

	/* BRender:
	 * Global arbitrary width scan line data
	 */
	struct arbitrary_width_scan awsl;

	/* BRender:
	 * Perspective texture mapper globals
	 */
	struct perspective_scan tsl;

	/* BRender:
	 * Base and range for decal
	 */
	br_int_32	decal_index_base;
	br_int_32	decal_index_range;
	br_int_32	decal_shade_height;

	/* BRender:
         * support for non textured materials that use the shade table
	 */

        br_int_32       index_base;
        br_int_32       index_range;

	/* BRender:
	 * Timestamps of input and output state that work buffers are based on
	 */
	br_uint_32 timestamp_prim;
	br_uint_32 timestamp_out;
} _prim_work;

extern _prim_work BR_ASM_DATA work;

/* BRender:
 * New line and point clamps
 */
#define CLAMP_LP(x,y) { \
  if (x<0) x = 0; \
  if (y<0) y = 0; \
  if (x>=work.colour.width_p) \
    x--; \
  if (y>=work.colour.height) \
    y--; \
}

typedef struct prim_work * BR_EXPORT br_work_fn(void);

#ifdef __cplusplus
};
#endif
#endif

