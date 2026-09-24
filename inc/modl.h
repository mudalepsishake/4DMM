/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: ***********************************************************************

    modl.h: Model class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BACO ---> MODL

*************************************************************************/
#ifndef MODL_H
#define MODL_H

// 3DMMv1.0: Model on file:
struct MODLF
{
    int16_t bo;
    int16_t osk;
    int16_t cver; // 3DMMv1.0: count of vertices
    int16_t cfac; // 3DMMv1.0: count of faces
    BRS rRadius;
    BRB brb; // 3DMMv1.0: bounds
    BVEC3 bvec3Pivot;
    // 3DMMv1.0:	br_vertex rgbrv[]; // vertices
    // 3DMMv1.0:	br_face rgbrf[]; // faces
};
VERIFY_STRUCT_SIZE(MODLF, 48);
typedef MODLF *PMODLF;
const BOM kbomModlf = 0x55fffff0;

/** 3DMMv1.0: **************************************
    MODL: a wrapper for BRender models
****************************************/
typedef class MODL *PMODL;
#define MODL_PAR BACO
#define kclsMODL KLCONST4('M', 'O', 'D', 'L')
class MODL : public MODL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    BMDL *_pbmdl; // 3DMMv1.0: BRender model data
    // actorlight24: immutable fingerprint of the unpacked source MODL chunk.
    // This lets BODY prove that models fetched through different CRF/CFL
    // wrappers are byte-for-byte the same source model without sharing any
    // actor-local BRender state. Generated/imported models leave this invalid.
    uint64_t _qwSourceHashA;
    uint64_t _qwSourceHashB;
    int32_t _cbSourceFingerprint;
    bool _fSourceFingerprint;
    // Modern BRender TDF glyphs need a one-time zero-pivot private prepare to
    // reproduce 3DMM's already-prepared legacy character geometry. Keep that
    // fact on the live wrapper so Create Part can serialize what is actually
    // being rendered instead of cloning the pre-bridge BMDL bytes.
    bool _fLegacyTdfPivotBridge;
  protected:
    MODL(void)
    {
        _qwSourceHashA = 0;
        _qwSourceHashB = 0;
        _cbSourceFingerprint = 0;
        _fSourceFingerprint = fFalse;
        _fLegacyTdfPivotBridge = fFalse;
    }
    bool _FInit(PBLCK pblck);
    bool _FPrelight(int32_t cblit, BVEC3 *prgbvec3Light);
    bool _FWrite(PCFL pcfl, CTG ctg, CNO cno, bool fZeroPivot);
    void _DumpUv(CTG ctg, CNO cno);

  public:
    static void SetUvDumpEnabled(bool fEnabled);
    static bool FUvDumpEnabled(void);
    static bool FSourceChunk(PBMDL pbmdl, CTG ctg, CNO cno);
    static void DumpBodyPart(PBMDL pbmdl, PBMTL pbmtl, const void *pvBody, int32_t ibact, int32_t ibset);
    static PMODL PmodlNew(int32_t cbrv, BRV *prgbrv, int32_t cbrf, BRF *prgbrf);
    static bool FReadModl(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);
    static PMODL PmodlReadFromDat(FNI *pfni);
    static PMODL PmodlFromBmdl(PBMDL pbmdl);
    ~MODL(void);
    PBMDL Pbmdl(void)
    {
        return _pbmdl;
    }
    bool FGetSourceFingerprint(int32_t *pcb, uint64_t *pqwHashA, uint64_t *pqwHashB)
    {
        if (!_fSourceFingerprint)
            return fFalse;
        if (pcb != pvNil)
            *pcb = _cbSourceFingerprint;
        if (pqwHashA != pvNil)
            *pqwHashA = _qwSourceHashA;
        if (pqwHashB != pvNil)
            *pqwHashB = _qwSourceHashB;
        return fTrue;
    }
    void AdjustTdfCharacter(void);
    void MarkLegacyTdfPivotBridge(void)
    {
        _fLegacyTdfPivotBridge = fTrue;
    }
    bool FLegacyTdfPivotBridge(void) const
    {
        return _fLegacyTdfPivotBridge;
    }
    bool FWrite(PCFL pcfl, CTG ctg, CNO cno);
    bool FWriteActorStudioBake(PCFL pcfl, CTG ctg, CNO cno);

    BRS Dxr(void)
    {
        return _pbmdl->bounds.max.v[0] - _pbmdl->bounds.min.v[0];
    }
    BRS Dyr(void)
    {
        return _pbmdl->bounds.max.v[1] - _pbmdl->bounds.min.v[1];
    }
    BRS Dzr(void)
    {
        return _pbmdl->bounds.max.v[2] - _pbmdl->bounds.min.v[2];
    }
};

#endif // 3DMMEx: TMPL_H
