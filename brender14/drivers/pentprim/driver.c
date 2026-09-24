/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: driver.c 1.1 1997/12/10 16:46:45 jon Exp $
 * $Locker: $
 *
 * Driver interface functions
 */
#include <stddef.h>
#include <string.h>

#include "drv.h"
#include "shortcut.h"
#include "brassert.h"


/* BRender:
 * Driver-wide timestamp
 */
br_uint_32 PrimDriverTimestamp;

/* BRender:
 * Main entry point for device - this may get redefined by the makefile
 */
br_device * BR_EXPORT BrDrv1SoftPrimBegin(const char *arguments)
{
	br_device *device;

	/* BRender:
	 * Setup timestamp
	 */
    if(PrimDriverTimestamp == 0)
    	PrimDriverTimestamp = TIMESTAMP_START;

	/* BRender:
	 * Set up device
	 */
#if BASED_FLOAT
    device = DeviceSoftPrimAllocate("SOFTPRMF");
#endif
#if BASED_FIXED
    device = DeviceSoftPrimAllocate("SOFTPRMX");
#endif

    if(device == NULL)
		return NULL;

	/* BRender:
	 * Setup primitive library
	 */
#if BASED_FLOAT
        if(PrimitiveLibrarySoftAllocate(device,"Default-Primitives-Float",arguments) == NULL) {
#endif
#if BASED_FIXED
        if(PrimitiveLibrarySoftAllocate(device,"Default-Primitives-Fixed",arguments) == NULL) {
#endif
        ObjectFree(device);
        return NULL;
    }

	return device;
}

#ifdef DEFINE_BR_ENTRY_POINT
br_device *BR_EXPORT BrDrv1Begin(const char *arguments)
{
    return BrDrv1SoftPrimBegin(arguments);
}
#endif
