/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    KidFrame #defines that might get used by a source file for a tool, such
    as source files for the chunky compiler.  This file should only contain
    #defines, and the values for the #defines should be constant values
    (no arithmetic).

***************************************************************************/
#ifndef KIDDEF_H
#define KIDDEF_H

/** 3DMMv1.0: *************************************************************************
    Cursor state constants.
        bits 0-7 are reserved by Frame.
        bits 8-15 are reserved by KidFrame.
        bits 16-31 are reserved for application use.
***************************************************************************/
#define fcustChildGok 256
#define fcustHotText 512 // 3DMMv1.0: over hot text in a help topic

/** 3DMMv1.0: *************************************************************************
    Standard command handler IDs.
        Frame reserves values < 10000.
        KidFrame reserves values >= 10000 and < 20000.
        Values >= 20000 can be used by the application.
***************************************************************************/
#define khidClokGokGen 10001   // 3DMMv1.0: for use by kidspace gobs
#define khidClokGokReset 10002 // 3DMMv1.0: for use by kidspace gobs - resets on input

#define khidLimKidFrame 20000

/** 3DMMv1.0: *************************************************************************
    Chunky file constants.
***************************************************************************/
#include "kidchk.h"

/** 3DMMv1.0: *************************************************************************
    GOK defines
***************************************************************************/
#define kdchidState 0x00010000 // 3DMMv1.0: this times the sno is the base chid
#define ksnoInit 1

// 3DMMv1.0: mouse state CHIDs - offsets from base chid
#define kchidOnState 0x0001    // 3DMMv1.0: bit indicating on/off state (1 for on)
#define kchidMouseState 0x0002 // 3DMMv1.0: bit indicating mouse state (1 for down)
#define kchidUpOff 0x0000      // 3DMMv1.0: 00
#define kchidUpOn 0x0001       // 3DMMv1.0: 01
#define kchidDownOff 0x0002    // 3DMMv1.0: 10
#define kchidDownOn 0x0003     // 3DMMv1.0: 11

// 3DMMv1.0: mouse transition CHIDs - offsets from base chid
// 3DMMv1.0: bit 4 set;  bits 2 & 3 are the from state; bits 1 & 0 are the to state
// 3DMMv1.0: note that 00 - 10 is not allowed
#define kchidTransBase 0x0010  // 3DMMv1.0: bit set for a transition state
#define kchidMaskDst 0x0003    // 3DMMv1.0: mask of destination state
#define kshSrcTrans 2          // 3DMMv1.0: number of bits to shift source state
#define kchidEnterState 0x0010 // 3DMMv1.0: this is a transitionary chid
#define kchidUpOffOn 0x0011    // 3DMMv1.0: 00 - 01 (roll on)
#define kchidUpOnOff 0x0014    // 3DMMv1.0: 01 - 00 (roll off)
#define kchidUpDownOn 0x0017   // 3DMMv1.0: 01 - 11 (mouse down event - start tracking)
#define kchidDownUpOn 0x001D   // 3DMMv1.0: 11 - 01 (click event - stop tracking)
#define kchidDownOnOff 0x001E  // 3DMMv1.0: 11 - 10 (continue tracking)
#define kchidDownOffOn 0x001B  // 3DMMv1.0: 10 - 11 (continue tracking)
#define kchidDownUpOff 0x0018  // 3DMMv1.0: 10 - 00 (stop mouse tracking)

// 3DMMv1.0: script CHIDs - offsets from base chid
#define kchidAlarm 0x0102

// 3DMMv1.0: types of GOKs that can be created
#define gokkNil 0
#define gokkRectangle 1 // 3DMMv1.0: all odd gokk's are rectangular
#define gokkNoHitThis 2 // 3DMMv1.0: this bit means it's invisible to the mouse
#define gokkNoHitKids 4 // 3DMMv1.0: this bit means its childrean are invis to mouse
#define gokkNoHit 6     // 3DMMv1.0: means it and its childrean are invis to mouse
#define gokkNoSlip 8    // 3DMMv1.0: anims for this GOK shouldn't slip (by default)

/** 3DMMv1.0: *************************************************************************
    Misc. constants
***************************************************************************/
#define kcnoToolTipNoAffect 0xFFFFFFFE

/** 3DMMv1.0: *************************************************************************
    Error codes
***************************************************************************/

/** 3DMMv1.0: ********************************************
    20000 - 29999: KidFrame-issued error codes
**********************************************/

// 3DMMv1.0: 20000 - 20099: help errors
#define ercHelpReadFailed 20000
#define ercHelpSaveFailed 20001

#endif //! 3DMMv1.0: KIDDEF_H
