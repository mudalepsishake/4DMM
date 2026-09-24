/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: sbuffer.h 1.1 1997/12/10 16:48:12 jon Exp $
 * $Locker: $
 *
 * Stored buffer structure
 */
#ifndef _SBUFFER_H_
#define _SBUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

/* BRender:
 * Flags
 */
enum {
	SBUFF_SHARED	= 0x0001,		/* BRender: Data is shared with user */
};

typedef struct br_buffer_stored {
	/* BRender:
	 * Dispatch table
	 */
	const struct br_buffer_stored_dispatch *dispatch;

	/* BRender:
	 * Standard object identifier
	 */
	char *identifier;

    /* BRender:
     * Pointer to owning device
     */
    struct br_device *device;

	/* BRender:
	 * Primitive library
	 */
	struct br_primitive_library *plib;

	/* BRender:
	 * Flags
	 */
	br_uint_32 flags;

	/* BRender:
	 * Reduced info about buffer
	 */
	struct render_buffer buffer;

	/* BRender:
	 * Duplicated pixelmap (or NULL if none)
	 */
	struct br_device_pixelmap *local_pm;

} br_buffer_stored;

#ifdef __cplusplus
};
#endif
#endif

