/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: pstate.h 1.1 1997/12/10 16:47:58 jon Exp $
 * $Locker: $
 *
 * Private primitive state structure
 */
#ifndef _PSTATE_H_
#define _PSTATE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* BRender:
 * Mask bits for state
 */
enum {
	MASK_STATE_OUTPUT		= BR_STATE_OUTPUT,
	MASK_STATE_PRIMITIVE	= BR_STATE_PRIMITIVE,
	MASK_STATE_CACHE		= BR_STATE_CACHE
};


/* BRender:
 * state.prim.flags
 */
enum {
	/* BRender:
	 * The public flags
	 */
	PRIMF_FORCE_FRONT_BIT,
	PRIMF_SMOOTH_BIT,
	PRIMF_DECAL_BIT,

	PRIMF_DITHER_COLOUR_BIT,
	PRIMF_DITHER_MAP_BIT,
	PRIMF_DEPTH_WRITE_BIT,

	PRIMF_INDEXED_COLOUR_BIT,
	PRIMF_BLEND_BIT,
	PRIMF_MODULATE_BIT,

	PRIMF_FOG_BIT,

	/* BRender:
	 * Internal flags using for matching
	 */
	PRIMF_OPAQUE_MAP_BIT,
	PRIMF_NO_SKIP_BIT,
	PRIMF_PERSPECTIVE_BIT,
	PRIMF_POWER2_BIT,
        PRIMF_STRIDE_POSITIVE_BIT,
	PRIMF_PALETTE_BIT,
        PRIMF_RANGE_ZERO_BIT,
};

enum {
	PRIMF_FORCE_FRONT		= (1<<PRIMF_FORCE_FRONT_BIT),
	PRIMF_SMOOTH			= (1<<PRIMF_SMOOTH_BIT),
	PRIMF_DECAL             = (1<<PRIMF_DECAL_BIT),
	PRIMF_DITHER_COLOUR		= (1<<PRIMF_DITHER_COLOUR_BIT),
	PRIMF_DITHER_MAP		= (1<<PRIMF_DITHER_MAP_BIT),
	PRIMF_DEPTH_WRITE		= (1<<PRIMF_DEPTH_WRITE_BIT),
	PRIMF_INDEXED_COLOUR    = (1<<PRIMF_INDEXED_COLOUR_BIT),
	PRIMF_BLEND             = (1<<PRIMF_BLEND_BIT),
	PRIMF_MODULATE		   	= (1<<PRIMF_MODULATE_BIT),
	PRIMF_FOG				= (1<<PRIMF_FOG_BIT),

	PRIMF_OPAQUE_MAP		= (1<<PRIMF_OPAQUE_MAP_BIT),
	PRIMF_NO_SKIP			= (1<<PRIMF_NO_SKIP_BIT),
	PRIMF_PERSPECTIVE		= (1<<PRIMF_PERSPECTIVE_BIT),
	PRIMF_POWER2			= (1<<PRIMF_POWER2_BIT),
        PRIMF_STRIDE_POSITIVE           = (1<<PRIMF_STRIDE_POSITIVE_BIT),
	PRIMF_PALETTE			= (1<<PRIMF_PALETTE_BIT),

	PRIMF_INTERNAL			= (	PRIMF_OPAQUE_MAP|
								PRIMF_NO_SKIP|
								PRIMF_PERSPECTIVE|
								PRIMF_POWER2|
                                                                PRIMF_PALETTE),
        PRIMF_RANGE_ZERO = (1<<PRIMF_RANGE_ZERO_BIT),

};

struct input_buffer {
	/* BRender:
	 * object on map
	 */
	struct br_buffer_stored *buffer;

	/* BRender:
	 * Cached info about current map - used to detect
	 * changes that will affect primitive lookup
	 */
	br_uint_32 width;
	br_uint_32 height;
	br_uint_32 stride;
	br_uint_32 type;
};

struct output_buffer {
	/* BRender:
	 * object on pixelmap
	 */
	struct br_device_pixelmap *pixelmap;

	/* BRender:
	 * Cached info about current buffer - used to detect
	 * changes that will affect primitive lookup
	 */
	br_uint_32 width;
	br_uint_32 height;
	br_uint_32 stride;
	br_uint_32 type;

	br_boolean viewport_changed;	/* BRender: True if viewport has been changed */
};

typedef struct br_primitive_state {
	/* BRender:
	 * Dispatch table
	 */
	const struct br_primitive_state_dispatch *dispatch;

	/* BRender:
	 * Standard object identifier
	 */
	const char *identifier;

    /* BRender:
     * Pointer to owning device
     */
    struct br_device *device;

    /* BRender:
     * Library that this state is attached to
     */
	struct br_primitive_library *plib;

	/* BRender:
	 * PRIMITIVE part of state
	 */
	struct {
		/* BRender:
		 * Timestamp for any change of this state
		 */
		br_uint_32 timestamp;

		/* BRender:
		 * Timestamp for any changes other than input buffer addresses
		 */
		br_uint_32 timestamp_major;

		/* BRender:
		 * flags
		 */
		br_uint_32 flags;

		/* BRender:
		 * User selected ramp
		 */
		br_int_32	index_base;
		br_int_32	index_range;
	
		/* BRender:
		 * Type of colour
		 */
		br_token	colour_type;

		/* BRender:
		 * Type of perspective correct rendering
		 */
		br_token	perspective_type;
		br_int_32	subdivide_tolerance;

		/* BRender:
		 * Current input buffers
		 */
		struct input_buffer colour_map;
		struct input_buffer index_shade;
		struct input_buffer index_blend;
		struct input_buffer index_fog;
		struct input_buffer screendoor;
		struct input_buffer lighting;
		struct input_buffer bump;

		/* BRender:
		 * Fog flag
		 */
		br_token        fog_type;

		/* BRender:
		 * Custom primitive block pointer
		 */
		struct local_block *custom_block;

	} prim;

	/* BRender:
	 * OUTPUT part of state
	 */
	struct {
		/* BRender:
		 * Timestamp for any change of this state
		 */
		br_uint_32 timestamp;

		/* BRender:
		 * Timestamp for any changes other than input buffer addresses
		 */
		br_uint_32 timestamp_major;

		/* BRender:
		 * Current output buffers
		 */
		struct output_buffer colour;
		struct output_buffer depth;

	} out;

	/* BRender:
	 * Cached info derived from rest of state
	 */
	struct {
		/* BRender:
		 * Last primitive block matched to this state
		 */
		struct local_block *last_block;
		br_token last_type;

		/* BRender:
		 * Ranges for components
		 */
		br_scalar comp_offsets[NUM_COMPONENTS];
		br_scalar comp_scales[NUM_COMPONENTS];

		/* BRender:
		 * Copies of timestamps for output and prim. states that
		 * block & ranges are based on
		 */
		br_uint_32 timestamp_prim;
		br_uint_32 timestamp_out;
	} cache;

} br_primitive_state;

#ifdef __cplusplus
};
#endif
#endif

