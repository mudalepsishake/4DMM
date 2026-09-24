/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

//
// 3DMMv1.0:  soc.h
//
// 3DMMv1.0:  Author: Sean Selitrennikoff
//
// 3DMMv1.0:  Date: August, 1994
//

#ifndef SOC_H
#define SOC_H

#include "frame.h"
#include "socdef.h"
#include "socutil.h"
#include "tagman.h"
#include "tagl.h"
#include "bren.h"
#include "tmap.h"
#include "modl.h"
#include "mtrl.h"
#include "body.h"
#include "tmpl.h"
#include "tdf.h"
#include "tdt.h"
#include "msnd.h"
#include "srec.h"
#include "actor.h"
#include "scene.h"
#include "movie.h"
#include "bkgd.h"
#include "tbox.h"

#define kctgActn KLCONST4('A', 'C', 'T', 'N')
#define kctgActr KLCONST4('A', 'C', 'T', 'R')
#define kctgBds KLCONST4('B', 'D', 'S', ' ')
#define kctgBkgd KLCONST4('B', 'K', 'G', 'D')
#define kctgBmdl KLCONST4('B', 'M', 'D', 'L')
#define kctgBpmp KLCONST4('B', 'P', 'M', 'P')
#define kctgCam KLCONST4('C', 'A', 'M', ' ')
#define kctgGgae KLCONST4('G', 'G', 'A', 'E')
#define kctgGldc KLCONST4('G', 'L', 'D', 'C') // 3DMMv1.0: REVIEW *****: obsolete
#define kctgGgcm KLCONST4('G', 'G', 'C', 'M')
#define kctgGllt KLCONST4('G', 'L', 'L', 'T')
#define kctgGlms KLCONST4('G', 'L', 'M', 'S') // 3DMMv1.0: motion-match sounds (under ACTN)
#define kctgGgcl KLCONST4('G', 'G', 'C', 'L')
#define kctgGlxf KLCONST4('G', 'L', 'X', 'F')
#define kctgMsnd KLCONST4('M', 'S', 'N', 'D')
#define kctgMtrl KLCONST4('M', 'T', 'R', 'L')
#define kctgT24M KLCONST4('T', '2', '4', 'M') // 4DMM: VXP2 truecolor texture metadata/path
#define kctgT24D KLCONST4('T', '2', '4', 'D') // 4DMM: movie-owned encoded truecolor texture data
#define kctgCmtl KLCONST4('C', 'M', 'T', 'L')
#define kctgMvie KLCONST4('M', 'V', 'I', 'E')
#define kctgPath KLCONST4('P', 'A', 'T', 'H')
#define kctgPict KLCONST4('P', 'I', 'C', 'T')
#define kctgScen KLCONST4('S', 'C', 'E', 'N')
#define kctgSnd KLCONST4('S', 'N', 'D', ' ')
#define kctgSoc KLCONST4('S', 'O', 'C', ' ')
#define kctgTbox KLCONST4('T', 'B', 'O', 'X')
#define kctgTdf KLCONST4('T', 'D', 'F', ' ')
#define kctgTdt KLCONST4('T', 'D', 'T', ' ')
#define kctgTmpl KLCONST4('T', 'M', 'P', 'L')
#define kctgGlpi KLCONST4('G', 'L', 'P', 'I')
#define kctgGlbs KLCONST4('G', 'L', 'B', 'S')
#define kctgInfo KLCONST4('I', 'N', 'F', 'O')
#define kctgFrmGg KLCONST4('G', 'G', 'F', 'R')
#define kctgStartGg KLCONST4('G', 'G', 'S', 'T')
#define kctgThumbMbmp KLCONST4('T', 'H', 'U', 'M')
#define kctgGltm KLCONST4('G', 'L', 'T', 'M')
#define kctgGlbk KLCONST4('G', 'L', 'B', 'K')
#define kctgGlcg KLCONST4('G', 'L', 'C', 'G')
#define kctgBkth KLCONST4('B', 'K', 'T', 'H') // 3DMMv1.0: Background thumbnail
#define kctgCath KLCONST4('C', 'A', 'T', 'H') // 3DMMv1.0: Camera thumbnail
#define kctgTmth KLCONST4('T', 'M', 'T', 'H') // 3DMMv1.0: Template thumbnail (non-prop)
#define kctgPrth KLCONST4('P', 'R', 'T', 'H') // 3DMMv1.0: Prop thumbnail
#define kctgAnth KLCONST4('A', 'N', 'T', 'H') // 3DMMv1.0: Action thumbnail
#define kctgSvth KLCONST4('S', 'V', 'T', 'H') // 3DMMv1.0: Sounds (voice) thumbnail
#define kctgSfth KLCONST4('S', 'F', 'T', 'H') // 3DMMv1.0: Sounds (FX) thumbnail
#define kctgSmth KLCONST4('S', 'M', 'T', 'H') // 3DMMv1.0: Sounds (midi) thumbnail
#define kctgMtth KLCONST4('M', 'T', 'T', 'H') // 3DMMv1.0: Materials thumbnail
#define kctgCmth KLCONST4('C', 'M', 'T', 'H') // 3DMMv1.0: Custom materials thumbnail
#define kctgTsth KLCONST4('T', 'S', 'T', 'H') // 3DMMv1.0: 3d shape thumbnail
#define kctgTfth KLCONST4('T', 'F', 'T', 'H') // 3DMMv1.0: 3d font thumbnail
#define kctgTcth KLCONST4('T', 'C', 'T', 'H') // 3DMMv1.0: Text color thumbnail
#define kctgTbth KLCONST4('T', 'B', 'T', 'H') // 3DMMv1.0: Text background thumbnail
#define kctgTzth KLCONST4('T', 'Z', 'T', 'H') // 3DMMv1.0: Text size thumbnail
#define kctgTyth KLCONST4('T', 'Y', 'T', 'H') // 3DMMv1.0: Text style thumbnail

#define khidMscb khidLimKidFrame
#define khidMvieClock khidLimKidFrame + 1
#define khidRcd khidLimKidFrame + 2
#define khidMsq khidLimKidFrame + 3
#define khidMsqClock khidLimKidFrame + 4
// 3DMMv1.0: khidStudio 				khidLimKidFrame + 5  Defined in stdiodef.h

#ifdef MAC
#define kftgChunky KLCONST4('c', 'h', 'n', 'k')
#define kftgContent KLCONST4('3', 'c', 'o', 'n')
#define kftgThumbDesc KLCONST4('3', 't', 'h', 'd')
#define kftg3mm KLCONST3('3', 'm', 'm')
#define kftgSocTemp KLCONST4('3', 't', 'm', 'p')
#else
#define kftgChunky KLCONST3('c', 'h', 'k')
#define kftgContent KLCONST3('3', 'c', 'n')
#define kftgThumbDesc KLCONST3('3', 't', 'h')
#define kftg3mm KLCONST3('3', 'm', 'm')
#define kftgSocTemp KLCONST3('3', 't', 'p')
#endif

#define ksz3mm PszLit("3mm")
#define kszVmm PszLit("vmm")
#define kftgVmm KLCONST3('v', 'm', 'm')

// 3DMMv1.0: Global variables
extern PTAGM vptagm;

#endif // 3DMMEx: !SOC_H
