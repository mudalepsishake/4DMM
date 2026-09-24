/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Contains the declarations of all globals used by util. The order of the
    globals within this file is critical. C++ specifies that constructors
    get called in source order within a module. It leaves construction
    order unspecified between modules. The mutexes at the top of this
    file should be constructed before anything else in the app. One way
    to guarantee this is for all globals to be in a single module. The
    app's global .cpp file can include utilglob.cpp (before any of its
    global declarations).

    For the MSVC tools, constructors seem to be executed in link order.

***************************************************************************/
#include "util.h"
ASSERTNAME

RTCLASS(USAC)

// 3DMMEx: Allocate globals in utilglob before any other globals to avoid crashes on exit
#pragma init_seg(lib)

#ifdef DEBUG
// 3DMMv1.0: protects our debug linked list object management
MUTX vmutxBase;
#endif // 3DMMv1.0: DEBUG

MUTX vmutxMem;

// 3DMMv1.0: Shuffler and random number generator for the script interpreter
SFL vsflUtil;
RND vrndUtil;

// 3DMMv1.0: Standard Kauai codec
KCDC vkcdcUtil;

// 3DMMv1.0: Standard compression manager - gets initialized with the standard
// 3DMMv1.0: Kauai codec. Clients can add additional codecs or redirect vpcodmUtil
// 3DMMv1.0: to a different compression manager with their own codecs
CODM vcodmUtil(&vkcdcUtil, kcfmtKauai2);
PCODM vpcodmUtil = &vcodmUtil;

// 3DMMv1.0: Standard scalable application clok.
USAC _usac;
PUSAC vpusac = &_usac;

#ifdef DEBUG

// 3DMMv1.0: Debug memory globals
DMGLOB vdmglob;

#endif // 3DMMv1.0: DEBUG
