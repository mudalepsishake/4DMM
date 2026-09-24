/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0:
 *
 * socutil.h
 *
 * This file contains miscellaneous includes and definitions
 * that are global to the Socrates product.
 *
 */

#ifndef SOCUTIL_H
#define SOCUTIL_H

extern "C"
{
// Modern BRender's compiler.h defines DEBUG to 0 in non-debug builds.
// Kauai/3DMM uses #ifdef DEBUG, so letting that macro escape BRender makes
// release translation units compile debug-only method bodies whose class
// declarations were omitted earlier. Preserve the application's DEBUG state
// across the BRender public header.
#if defined(DEBUG)
#define THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER 1
#endif
#include "brender.h"
#if defined(BRENDER_MODERN_14) && !defined(THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER) && defined(DEBUG)
#undef DEBUG
#endif
#ifdef THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER
#undef THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER
#endif
};

typedef class ACTR *PACTR;
typedef class SCEN *PSCEN;
typedef class MVIE *PMVIE;
typedef struct OBJECTGROUPSTATE OBJECTGROUPSTATE;
typedef class BKGD *PBKGD;
typedef class TBOX *PTBOX;
typedef class MVIEW *PMVIEW;
typedef class STDIO *PSTDIO;

//
//
// 3DMMv1.0: Class for undo items in a movie
//
// 3DMMv1.0: NOTE: All the "Set" functions are done automagically
// 3DMMv1.0: in MVIE::FAddUndo().
//
//
typedef class MUNB *PMUNB;

#define MUNB_PAR UNDB
#define kclsMUNB KLCONST4('M', 'U', 'N', 'B')
class MUNB : public MUNB_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    PMVIE _pmvie;
    int32_t _iscen;
    int32_t _nfrm;

    MUNB(void)
    {
    }

  public:
    void SetPmvie(PMVIE pmvie)
    {
        _pmvie = pmvie;
    }
    PMVIE Pmvie(void)
    {
        return _pmvie;
    }

    void SetIscen(int32_t iscen)
    {
        _iscen = iscen;
    }
    int32_t Iscen(void)
    {
        return _iscen;
    }

    void SetNfrm(int32_t nfrm)
    {
        _nfrm = nfrm;
    }
    int32_t Nfrm(void)
    {
        return _nfrm;
    }
};

//
// 3DMMv1.0: Undo object for actor operations
//
typedef class AUND *PAUND;

#define AUND_PAR MUNB
#define kclsAUND KLCONST4('A', 'U', 'N', 'D')
class AUND : public AUND_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    PACTR _pactr;
    int32_t _arid;
    bool _fSoonerLater;
    bool _fSndUndo;
    int32_t _nfrmLast;
    STN _stn; // 3DMMv1.0: actor's name

    // Scene-wide Light Lab metadata follows an actor through complete-delete
    // undo/redo.  Ordinary actor edits leave this snapshot untouched.
    bool _fHadLightLab;
    int32_t _iscenLightLab;
    bool _fLightEnabled;
    bool _fLightGenerateShadows;
    bool _fLightAttachmentHideable;
    int32_t _lightIntensity;
    float _lightEdgeGradient;
    float _lightDiameter;
    float _lightRange;
    achar _szLightShape[16];

    AUND(void)
    {
        _pactr = pvNil;
        _arid = ivNil;
        _fSoonerLater = fFalse;
        _fSndUndo = fFalse;
        _nfrmLast = 0;
        _fHadLightLab = fFalse;
        _iscenLightLab = ivNil;
        _fLightEnabled = fFalse;
        _fLightGenerateShadows = fFalse;
        _fLightAttachmentHideable = fFalse;
        _lightIntensity = 100;
        _lightEdgeGradient = 4.0f;
        _lightDiameter = 24.0f;
        _lightRange = 500.0f;
        _szLightShape[0] = chNil;
    }

  public:
    static PAUND PaundNew(void);
    ~AUND(void);

    void SetPactr(PACTR pactr);
    void SetArid(int32_t arid)
    {
        _arid = arid;
    }
    void SetSoonerLater(bool fSoonerLater)
    {
        _fSoonerLater = fSoonerLater;
    }
    void SetSndUndo(bool fSndUndo)
    {
        _fSndUndo = fSndUndo;
    }
    void SetNfrmLast(int32_t nfrmLast)
    {
        _nfrmLast = nfrmLast;
    }
    void SetStn(PSTN pstn)
    {
        _stn = *pstn;
    }
    void CaptureLightLab(PMVIE pmvie, int32_t iscen, int32_t arid);
    void RestoreLightLab(void);
    void RemoveLightLab(void);

    bool FSoonerLater(void)
    {
        return _fSoonerLater;
    };
    bool FSndUndo(void)
    {
        return _fSndUndo;
    };

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};


//
// Undo object for a bound -multi Object Group transform.  Each entry owns a
// duplicate of one actor's complete edit state.  Undo/redo swaps those states
// in place so the group is one history operation rather than N actor edits.
//
typedef class GUND *PGUND;

#define GUND_PAR AUND
#define kclsGUND KLCONST4('G', 'U', 'N', 'D')
struct GUNDENTRY
{
    PAUND paund;
};

class GUND : public GUND_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    PGL _pglentry;
    OBJECTGROUPSTATE *_pogstate;
    STN _stnGroupUndoName;
    int32_t _idGroup;

    GUND(void)
    {
        _pglentry = pvNil;
        _pogstate = pvNil;
        _idGroup = 0;
    }

  public:
    static PGUND PgundNew(void);
    ~GUND(void);

    bool FCaptureGroup(PMVIE pmvie, int32_t idGroup);
    bool FCaptureGroupDelete(PMVIE pmvie, int32_t idGroup);
    bool FCaptureMembership(PMVIE pmvie, const achar *pszUndoName);
    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

//
// 3DMMv1.0: Definition of transition types
//
enum TRANS
{
    transNil = -1,
    transCut,
    transFadeToBlack,
    transFadeToWhite,
    transDissolve,
    transBlack,
    transLim
};

#endif // 3DMMv1.0: SOCUTIL_H
