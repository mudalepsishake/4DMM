/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    ape.h: Actor preview entity

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> CMH ---> GOB ---> APE

***************************************************************************/
#ifndef APE_H
#define APE_H

// 3DMMv1.0: APE tool types
enum
{
    aptNil = 0,
    aptIncCmtl,      // 3DMMv1.0: Increment CMTL
    aptIncAccessory, // 3DMMv1.0: Increment Accessory
    aptGms,          // 3DMMv1.0: Material (MTRL or CMTL)
    aptLim
};

// 3DMMv1.0: Generic material spec
struct GMS
{
    bool fValid; // 3DMMv1.0: if fFalse, ignore this GMS
    bool fMtrl;  // 3DMMv1.0: if fMtrl is fTrue, tagMtrl is valid.  Else cmid is valid
    int32_t cmid;
    TAG tagMtrl;
};

// 3DMMv1.0: Actor preview entity tool
struct APET
{
    int32_t apt;
    GMS gms;
};

/** 3DMMv1.0: **************************************
    Actor preview entity class
****************************************/
typedef class APE *PAPE;
#define APE_PAR GOB
#define kclsAPE KLCONST3('A', 'P', 'E')
class APE : public APE_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(APE)

  protected:
    PBWLD _pbwld;          // 3DMMv1.0: BRender world to draw actor in
    PTMPL _ptmpl;          // 3DMMv1.0: Template (or TDT) of the actor being previewed
    PBODY _pbody;          // 3DMMv1.0: Body of the actor being previewed
    APET _apet;            // 3DMMv1.0: Currently selected tool
    PGL _pglgms;           // 3DMMv1.0: What materials are attached to what body part sets
    int32_t _celn;         // 3DMMv1.0: Current cel of action
    CLOK _clok;            // 3DMMv1.0: To time cel cycling
    BLIT _blit;            // 3DMMv1.0: BRender light data
    BACT _bact;            // 3DMMv1.0: BRender light actor
    int32_t _anid;         // 3DMMv1.0: Current action ID
    int32_t _iview;        // 3DMMv1.0: Current camera view
    bool _fCycleCels;      // 3DMMv1.0: If cycling cels
    bool _fModernFirstDrawRefresh; // rerender after final easel layout in Modern mode
    PRCA _prca;            // 3DMMv1.0: resource source (for cursors)
    int32_t _ibsetOnlyAcc; // 3DMMv1.0: ibset of accessory, if only one (else ivNil)

  protected:
    APE(PGCB pgcb) : GOB(pgcb), _clok(CMH::HidUnique())
    {
    }
    bool _FInit(PTMPL ptmpl, PCOST pcost, int32_t anid, bool fCycleCels, PRCA prca);
    void _InitView(void);
    void _SetScale(void);
    void _UpdateView(void);
    bool _FApplyGms(GMS *pgms, int32_t ibset);
    bool _FIncCmtl(GMS *pgms, int32_t ibset, bool fNextAccessory);
    int32_t _CmidNext(int32_t ibset, int32_t icmidCur, bool fNextAccessory);

  public:
    static PAPE PapeNew(PGCB pgcb, PTMPL ptmpl, PCOST pcost, int32_t anid, bool fCycleCels, PRCA prca = pvNil);
    ~APE();

    void SetToolMtrl(PTAG ptagMtrl);
    void SetToolCmtl(int32_t cmid);
    void SetToolIncCmtl(void);
    void SetToolIncAccessory(void);

    bool FSetAction(int32_t anid);
    bool FCmdNextCel(PCMD pcmd);

    void SetCustomView(BRA xa, BRA ya, BRA za);
    void ChangeView(void);
    virtual void Draw(PGNV pgnv, RC *prcClip) override;
    virtual bool FCmdMouseMove(PCMD_MOUSE pcmd) override;
    virtual bool FCmdTrackMouse(PCMD_MOUSE pcmd) override;

    bool FChangeTdt(PSTN pstn, int32_t tdts, PTAG ptagTdf);
    bool FSetTdtMtrl(PTAG ptagMtrl);
    bool FApplyMtrlToBset(int32_t ibset, PTAG ptagMtrl);
    bool FGetTdtMtrlCno(CNO *pcno);

    void GetTdtInfo(PSTN pstn, int32_t *ptdts, PTAG ptagTdf);
    int32_t Anid(void)
    {
        return _anid;
    }
    int32_t Celn(void)
    {
        return _celn;
    }
    void SetCycleCels(bool fOn);
    bool FIsCycleCels(void)
    {
        return _fCycleCels;
    }
    bool FDisplayCel(int32_t celn);
    int32_t Cbset(void)
    {
        return _pbody->Cbset();
    }

    // 3DMMv1.0: Returns fTrue if a material was applied to this ibset
    bool FGetMaterial(int32_t ibset, tribool *pfMtrl, int32_t *pcmid, TAG *ptagMtrl);
};

#endif // 3DMMEx: APE_H
