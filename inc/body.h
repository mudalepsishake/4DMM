/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: ***********************************************************************

    body.h: Body class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BODY

*************************************************************************/
#ifndef BODY_H
#define BODY_H

/** 3DMMv1.0: **************************************
    The BODY class
****************************************/
typedef class BODY *PBODY;
typedef bool (*PFNBODYCLICKFILTER)(PBODY pbody, void *pvContext);
#define BODY_PAR BASE
#define kclsBODY KLCONST4('B', 'O', 'D', 'Y')
class BODY : public BODY_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    static BMTL *_pbmtlHilite; // 3DMMv1.0: hilight material
    static BODY *_pbodyClosestClicked;
    static int32_t _dzpClosestClicked;
    static BACT *_pbactClosestClicked;
    BACT *_prgbact;      // 3DMMv1.0: array of BACTs
    PGL _pglibset;       // 3DMMv1.0: body part set IDs
    int32_t _cbset;      // 3DMMv1.0: count of body part sets
    PCMTL *_prgpcmtl;    // 3DMMv1.0: array of PCMTLs -- one per body part set
    int32_t _cbactPart;  // 3DMMv1.0: count of model body parts in body
    int32_t _cactHidden; // 3DMMv1.0: for Show() / Hide()
    PBWLD _pbwld;        // 3DMMv1.0: world that body lives in
    RC _rcBounds;        // 3DMMv1.0: bounds of body after last render
    RC _rcBoundsLastVis; // 3DMMv1.0: bounds of body last time it was visible
    bool _fFound;        // 3DMMv1.0: is the actor found under the mouse?
    int32_t _ibset;      // 3DMMv1.0: which body part got hit.
    uint32_t _grf4DMMShadowProperties; // runtime BODY-root shadow/render policy bits

  protected:
    BODY(void)
    {
        _grf4DMMShadowProperties = 0;
    } // 3DMMv1.0: can't instantiate directly; must use PbodyNew
    void _DestroyShape(void);
    bool _FInit(PGL pglibactPar, PGL pglibset);
    bool _FInitShape(PGL pglibactPar, PGL pglibset);
    PBACT _PbactRoot(void) // 3DMMv1.0: ptr to root BACT
    {
        return _prgbact;
    }
    PBACT _PbactHilite(void) // 3DMMv1.0: ptr to hilite BACT
    {
        return _prgbact + 1;
    }                               // 3DMMv1.0: skip root BACT
    PBACT _PbactPart(int32_t ipart) // 3DMMv1.0: ptr to ipart'th body part
    {
        return _prgbact + 1 + 1 + ipart;
    }                    // 3DMMv1.0: skip root and hilite BACTs
    int32_t _Cbact(void) // 3DMMv1.0: count in _prgbact
    {
        return 1 + 1 + _cbactPart;
    }                             // 3DMMv1.0: root, hilite, and body part BACTs
    int32_t _Ibset(int32_t ipart) // 3DMMv1.0: body part set that this part belongs to
    {
        return *(int16_t *)_pglibset->QvGet(ipart);
    }
    void _RemoveMaterial(int32_t ibset);

    // 3DMMv1.0: Callbacks from BRender:
    static int BR_CALLBACK _FFilterSearch(BACT *pbact, PBMDL pbmdl, PBMTL pbmtl, BVEC3 *pbvec3RayPos,
                                          BVEC3 *pbvec3RayDir, BRS dzpNear, BRS dzpFar, void *pvArg);
    static void _BactRendered(PBACT pbact, RC *prc);
    static void _PrepareToRender(PBACT pbact);
    static void _GetRc(PBACT pbact, RC *prc);

  public:
    static PBODY PbodyNew(PGL pglibactPar, PGL pglibset);
    static PBODY PbodyFromBact(BACT *pbact, int32_t *pibset = pvNil);
    static PBODY PbodyClicked(int32_t xp, int32_t yp, PBWLD pbwld, int32_t *pibset = pvNil,
                              PFNBODYCLICKFILTER pfnFilter = pvNil, void *pvContext = pvNil);
    ~BODY(void);
    PBODY PbodyDup(void);
    void Restore(PBODY pbodyDup);
    static int BR_CALLBACK _FFilter(BACT *pbact, PBMDL pbmdl, PBMTL pbmtl, BVEC3 *pbvec3RayPos, BVEC3 *pbvec3RayDir,
                                    BRS dzpNear, BRS dzpFar, void *pv);

    bool FChangeShape(PGL pglibactPar, PGL pglibset);
    void SetBwld(PBWLD pbwld)
    {
        _pbwld = pbwld;
    }

    void Show(void);
    void Hide(void);
    bool FVisible(void)
    {
        AssertBaseThis(0);
        return _cactHidden == 0;
    }
    void Hilite(void);
    void Unhilite(void);

    // 4DMM Actor Studio: use BODY's existing bounding-edge actor to highlight
    // one real articulated BACT without adding any fields to BODY.
    void HilitePart(int32_t ipart);
    void HilitePartRange(int32_t ipartFirst, int32_t cpart);
    void HilitePartTree(int32_t ipart); // Actor Studio Pt Group: part plus descendant subtree
    void ClearPartHilite(void);

    static void SetHiliteColor(int32_t iclr);
    static int32_t ImodHilite(void);
    static int32_t CycleHiliteMode(void);

    void LocateOrient(BRS xr, BRS yr, BRS zr, BMAT34 *pbmat34);
    void SetPartModel(int32_t ibact, MODL *pmodl);
    void ClearPartModel(int32_t ibact); // 4DMM Actor Studio temporal Cut
    bool FPartHasModel(int32_t ibact)
    {
        AssertThis(0);
        AssertIn(ibact, 0, _cbactPart);
        return _PbactPart(ibact)->model != pvNil;
    }
    void SetPartMatrix(int32_t ibact, BMAT34 *pbmat34);
    void SetPartSetMtrl(int32_t ibset, MTRL *pmtrl);
    void SetPartSetCmtl(CMTL *pcmtl);
    void GetPartSetMaterial(int32_t ibset, bool *pfMtrl, MTRL **ppmtrl, CMTL **ppcmtl);
    int32_t Cbset()
    {
        return _cbset;
    }

    // Actor Studio inspection bridge. BODY already owns the authoritative
    // BRender part hierarchy; expose read-only topology/matrix queries so
    // 4DMM can inspect real articulated parts instead of treating material
    // body-part sets as if they were joints. These add no fields to BODY.
    int32_t Cpart(void)
    {
        return _cbactPart;
    }
    int32_t IpartFromBact(PBACT pbact);
    int32_t IpartParent(int32_t ipart);
    int32_t IbsetOfPart(int32_t ipart);
    void GetPartMatrix(int32_t ipart, BMAT34 *pbmat34);
    bool FGetPartModelBounds(int32_t ipart, BRB *pbrb);
    bool FGetPartModelGeometry(int32_t ipart, int32_t *pcver, int32_t *pcfac);
    // Create Part synthesis bridges. FGetPartBakeTransform works for every
    // BODY node, including model-less hierarchy/group nodes. FGetPartBakeData
    // additionally returns AddRef'd wrappers for a visible part's current model
    // and material. A null ancestor means BODY-root space.
    bool FGetPartBakeTransform(int32_t ipart, PBACT pbactAncestor, BMAT34 *pbmat34);
    bool FGetPartBakeData(int32_t ipart, PBACT pbactAncestor, PMODL *ppmodl,
                          PMTRL *ppmtrl, BMAT34 *pbmat34);

    void GetBcbBounds(BCB *pbcb, bool fWorld = fFalse);
    void GetRcBounds(RC *prc);
    void GetCenter(int32_t *pxp, int32_t *pyp);
    void GetPosition(BRS *pxr, BRS *pyr, BRS *pzr);
    void GetMatrix(BMAT34 *pbmat34)
    {
        AssertThis(0);
        AssertVarMem(pbmat34);
        *pbmat34 = _PbactRoot()->t.t.mat;
    }
    bool FPtInBody(int32_t xp, int32_t yp, int32_t *pibset);
    bool FIsInView(void);
};

// 4DMM shadow-policy bridge.  The BODY root owns the runtime flag word so
// BRender can inherit object-wide policy across every BODY part without
// changing the historical br_actor ABI.
void Set4DMMBodyShadowCasterExcluded(PBODY pbody, bool fExclude);
void Set4DMMBodyObjectShadowProperties(PBODY pbody, bool fCastShadows, bool fFlushOverlap);

/** 3DMMv1.0: **************************************
    The COST class, which is used to
    save and restore a BODY's entire
    costume for unwinding purposes
****************************************/
typedef class COST *PCOST;
#define COST_PAR BASE
#define kclsCOST KLCONST4('C', 'O', 'S', 'T')
class COST : public COST_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    int32_t _cbset; // 3DMMv1.0: count of body part sets in _prgpo
    BASE **_prgpo;  // 3DMMv1.0: array of MTRLs and CMTLs

  private:
    void _Clear(void); // 3DMMv1.0: release _prgpo and material references

  public:
    COST(void);
    ~COST(void);

    bool FGet(PBODY pbody); // 3DMMv1.0: read and store BODY's costume

    // 3DMMv1.0: replace BODY's costume with this one
    void Set(PBODY pbody, bool fAllowDifferentShape = fFalse);
};

#endif // 3DMMv1.0: !BODY_H
