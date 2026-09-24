/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    bkgd.h: Background class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BACO ---> BKGD

***************************************************************************/
#ifndef BKGD_H
#define BKGD_H

/** 3DMMv1.0: **************************************
    Background on file
****************************************/
struct BKGDF
{
    int16_t bo;
    int16_t osk;
    uint8_t bIndexBase;
    uint8_t bPad;
    int16_t swPad;
};
VERIFY_STRUCT_SIZE(BKGDF, 8);
const BOM kbomBkgdf = 0x50000000;

/** 3DMMv1.0: **************************************
    Specifies a light's kind, position,
    orientation, and brightness
****************************************/
struct LITE
{
    BMAT34 bmat34;
    BRS rIntensity;
    int32_t lt; // 3DMMv1.0: light type
};
VERIFY_STRUCT_SIZE(LITE, 56);
const BOM kbomLite = 0xfffffff0;

/** 3DMMv1.0: **************************************
    Specifies a camera for a view
****************************************/
typedef union _apos {
    struct
    {
        BRS xrPlace; // 3DMMv1.0: Initial Actor Placement point
        BRS yrPlace;
        BRS zrPlace;
    };
    BVEC3 bvec3Actor;
} APOS;
VERIFY_STRUCT_SIZE(APOS, 12);

struct CAM
{
    int16_t bo;
    int16_t osk;
    BRS zrHither; // 3DMMv1.0: Hither (near) plane
    BRS zrYon;    // 3DMMv1.0: Yon (far) plane
    BRA aFov;     // 3DMMv1.0: Field of view
    int16_t swPad;
    APOS apos;
    BMAT34 bmat34Cam; // 3DMMv1.0: Camera view matrix
    // 3DMMv1.0: APOS rgapos[];
};
VERIFY_STRUCT_SIZE(CAM, 76);
const BOM kbomCamOld = 0x5f4fc000;
const BOM kbomCam = BomField(
    kbomSwapShort,
    BomField(kbomSwapShort,
             BomField(kbomSwapLong,
                      BomField(kbomSwapLong,
                               BomField(kbomSwapShort,
                                        BomField(kbomLeaveShort,
                                                 BomField(kbomSwapLong,
                                                          BomField(kbomSwapLong, BomField(kbomSwapLong, 0)))))))));

// 3DMMv1.0: Note that CAM is too big for a complete kbomCam.  To SwapBytes one,
// 3DMMv1.0: SwapBytesBom the cam, then SwapBytesRgLw from bmat34Cam on.

/** 3DMMv1.0: **************************************
    Background Default Sound
****************************************/

// 3DMMEx: On-disk representation of BDS
struct BDSF
{
    int16_t bo;
    int16_t osk;
    int32_t vlm;
    bool fLoop;
    TAGF tagSnd;
};
VERIFY_STRUCT_SIZE(BDSF, 28);
const BOM kbomBds = 0x5f000000 | kbomTag >> 8;

struct BDS
{
    int32_t vlm;
    bool fLoop;
    TAG tagSnd;
};

/** 3DMMv1.0: **************************************
    The background class
****************************************/
typedef class BKGD *PBKGD;
#define BKGD_PAR BACO
#define kclsBKGD KLCONST4('B', 'K', 'G', 'D')
class BKGD : public BKGD_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    BACT *_prgbactLight; // 3DMMv1.0: array of light br_actors
    BLIT *_prgblitLight; // 3DMMv1.0: array of light data
    int32_t _cbactLight; // 3DMMv1.0: count of lights
    bool _fLites;        // 3DMMv1.0: lights are on
    bool _fLeaveLitesOn; // 3DMMv1.0: Don't turn out the lights
    int32_t _ccam;       // 3DMMv1.0: count of cameras in this background
    int32_t _icam;       // 3DMMv1.0: current camera
    BMAT34 _bmat34Mouse; // 3DMMv1.0: camera matrix for mouse model
    BRA _braRotY;        // 3DMMv1.0: Y rotation of current camera
    CNO _cnoSnd;         // 3DMMv1.0: background sound
    STN _stn;            // 3DMMv1.0: name of this background
    PGL _pglclr;         // 3DMMv1.0: palette for this background
    uint8_t _bIndexBase; // 3DMMv1.0: first index for palette
    int32_t _iaposLast;  // 3DMMv1.0: Last placement point we used
    int32_t _iaposNext;  // 3DMMv1.0: Next placement point to use
    PGL _pglapos;        // 3DMMv1.0: actor placement point(s) for current view
    BRS _xrPlace;
    BRS _yrPlace;
    BRS _zrPlace;
    BDS _bds;   // 3DMMv1.0: background default sound
    BRS _xrCam; // 3DMMv1.0: camera position in worldspace
    BRS _yrCam;
    BRS _zrCam;

    // Exact, unmodified camera state selected by vanilla 3DMM.  Camera-track
    // offsets are always applied to this state so movement never accumulates
    // from one rendered frame to the next.
    BMAT34 _bmat34CamBase;
    BRS _zrHitherCamBase;
    BRS _zrYonCamBase;
    BRA _aFovCamBase;
    bool _fCamBaseValid;

  protected:
    bool _FInit(PCFL pcfl, CTG ctg, CNO cno);
    int32_t _Ccam(PCFL pcfl, CTG ctg, CNO cno);
    void _SetupLights(PGL pgllite);

  public:
    static bool FAddTagsToTagl(PTAG ptagBkgd, PTAGL ptagl);
    static bool FCacheToHD(PTAG ptagBkgd);
    static bool FReadBkgd(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);
    ~BKGD(void);
    void GetName(PSTN pstn);

    void TurnOnLights(PBWLD pbwld);
    void TurnOffLights(void);
    bool FLeaveLitesOn(void)
    {
        return _fLeaveLitesOn;
    }
    void SetFLeaveLitesOn(bool fLeaveLitesOn)
    {
        _fLeaveLitesOn = fLeaveLitesOn;
    }

    int32_t Ccam(void)
    {
        return _ccam;
    } // 3DMMv1.0: count of cameras in background
    int32_t Icam(void)
    {
        return _icam;
    }                                           // 3DMMv1.0: currently selected camera
    bool FSetCamera(PBWLD pbwld, int32_t icam); // 3DMMv1.0: change camera to icam

    void GetMouseMatrix(BMAT34 *pbmat34);
    BRA BraRotYCamera(void)
    {
        return _braRotY;
    }
    void GetActorPlacePoint(BRS *pxr, BRS *pyr, BRS *pzr);
    void GetDefaultActorPlacePoint(BRS *pxr, BRS *pyr, BRS *pzr);
    void ReuseActorPlacePoint(void);

    void GetDefaultSound(PTAG ptagSnd, int32_t *pvlm, bool *pfLoop)
    {
        *ptagSnd = _bds.tagSnd;
        *pvlm = _bds.vlm;
        *pfLoop = _bds.fLoop;
    }

    bool FGetPalette(PGL *ppglclr, int32_t *piclrMin);
    void GetCameraPos(BRS *pxr, BRS *pyr, BRS *pzr);
    bool FGetCameraBase(BMAT34 *pbmat34, BRS *pzrHither, BRS *pzrYon, BRA *paFov);

#ifdef DEBUG
    // 3DMMv1.0: Authoring only.  Writes a special file with the given place info.
    bool FWritePlaceFile(BRS xrPlace, BRS yrPlace, BRS zrPlace);
#endif // 3DMMv1.0: DEBUG
};

#endif // 3DMMEx: BKGD_H
