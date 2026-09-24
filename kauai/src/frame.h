/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Main include file for frame files.

***************************************************************************/
#ifndef FRAME_H
#define FRAME_H

#include "frameres.h" // 3DMMv1.0: frame resource id's
#include "util.h"
#include "keys.h"

class GPT;  // 3DMMv1.0: graphics port
class GNV;  // 3DMMv1.0: graphics environment
class CMH;  // 3DMMv1.0: command handler
class GOB;  // 3DMMv1.0: graphic object
class MUB;  // 3DMMv1.0: menu bar
class DOCB; // 3DMMv1.0: base document
class DMD;  // 3DMMv1.0: document mdi window
class DMW;  // 3DMMv1.0: main document window
class DSG;  // 3DMMv1.0: document scroll gob
class DDG;  // 3DMMv1.0: document display gob
class SNDM; // 3DMMv1.0: sound manager

typedef class GPT *PGPT;
typedef class GNV *PGNV;
typedef class CMH *PCMH;
typedef class GOB *PGOB;
typedef class MUB *PMUB;
typedef class DOCB *PDOCB;
typedef class DMD *PDMD;
typedef class DMW *PDMW;
typedef class DSG *PDSG;
typedef class DDG *PDDG;
typedef class SNDM *PSNDM;

#include "kwnd.h"
#include "region.h"
#include "pic.h"
#include "mbmp.h"
#include "gfx.h"
#include "cmd.h"
#include "cursor.h"
#include "appb.h"
#include "menu.h"
#include "gob.h"
#include "ctl.h"
#include "sndm.h"
#include "video.h"

// 3DMMv1.0: these are optional
#include "dlg.h"
#include "clip.h"
#include "docb.h"
#include "text.h"
#include "clok.h"
#include "textdoc.h"
#include "rtxt.h"
#include "chcm.h"
#include "spell.h"
#include "sndam.h"
#include "mididev.h"
#include "mididev2.h"
#include "accelerator.h"

#endif //! 3DMMv1.0: FRAME_H
