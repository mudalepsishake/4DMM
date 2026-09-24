/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: defdiag.c 1.1 1997/12/10 16:41:05 jon Exp $
 * $Locker: $
 *
 * Default diagnostic handler that does nothing
 */
#include "brender.h"

static void BrNullWarning(const char *message)
{
}

static void BrNullFailure(const char *message)
{
}

/* BRender:
 * DiagHandler structure
 */
br_diaghandler BrNullDiagHandler = {
    "Null DiagHandler",
    BrNullWarning,
    BrNullFailure,
};

// BRenderModern: Nope, we're using std now
/// BRenderModern: *
// BRender: * Global variable that can be overridden by linking something first
// BRenderModern: */
// BRenderModern: br_diaghandler *_BrDefaultDiagHandler = &BrNullDiagHandler;
