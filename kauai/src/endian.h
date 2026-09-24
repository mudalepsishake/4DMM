/* 3DMMEx: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMEx: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMEx: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Endian include file.

***************************************************************************/
#ifndef ENDIAN_H
#define ENDIAN_H

// 3DMMEx: define the endian-ness
#ifdef IN_80386
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN
#endif // 3DMMEx: LITTLE_ENDIAN
#endif // 3DMMEx: IN_80386

#ifdef LITTLE_ENDIAN
#define BigLittle(a, b) b
#define Big(a)
#define Little(a) a
#else //! 3DMMEx: LITTLE_ENDIAN
#define BigLittle(a, b) a
#define Big(a) a
#define Little(a)
#endif //! 3DMMEx: LITTLE_ENDIAN

#endif
