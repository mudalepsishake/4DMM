/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Menu bar management.

***************************************************************************/
#ifndef MENU_H
#define MENU_H

// 3DMMv1.0: Menu Bar class
typedef class MUB *PMUB;
#define MUB_PAR BASE
#define kclsMUB KLCONST3('M', 'U', 'B')
class MUB : public MUB_PAR
{
    RTCLASS_DEC
    MARKMEM

  private:
#ifdef MAC
    // 3DMMv1.0: System Menu
    typedef MenuInfo SMU;

    // 3DMMv1.0: System Menu Bar
    struct SMB
    {
        ushort cmid;
        ushort rgmid[1];
    };

    // 3DMMv1.0: Menu Item
    struct MNI
    {
        int32_t cid;
        int32_t lw0;
    };

    // 3DMMv1.0: Menu
    struct MNU
    {
        int32_t mid;
        SMU **hnsmu;
        PGL pglmni;
    };

    // 3DMMv1.0: menu list
    struct MLST
    {
        int32_t imnu;
        int32_t imniBase;
        int32_t cmni;
        int32_t cid;
        bool fSeparator;
    };

    HN _hnmbar;
    PGL _pglmnu;
    PGL _pglmlst; // 3DMMv1.0: menu lists

    bool _FInsertMni(int32_t imnu, int32_t imni, int32_t cid, int32_t lw0, PSTN pstn);
    void _DeleteMni(int32_t imnu, int32_t imni);
    bool _FFindMlst(int32_t imnu, int32_t imni, MLST *pmlst = pvNil, int32_t *pimlst = pvNil);
    bool _FGetCmdFromCode(int32_t lwCode, CMD *pcmd);
    void _Free(void);
    bool _FFetchRes(uint32_t ridMenuBar);
#endif // 3DMMv1.0: MAC

#ifdef KAUAI_WIN32
    // 3DMMv1.0: menu list
    struct MLST
    {
        HMENU hmenu;
        int32_t imniBase;
        int32_t wcidList;
        int32_t cid;
        bool fSeparator;
        PGL pgllw;
    };

    HMENU _hmenu;  // 3DMMv1.0: the menu bar
    int32_t _cmnu; // 3DMMv1.0: number of menus on the menu bar
    PGL _pglmlst;  // 3DMMv1.0: menu lists

    bool _FInitLists(void);
    bool _FFindMlst(int32_t wcid, MLST *pmlst, int32_t *pimlst = pvNil);
    bool _FGetCmdForWcid(int32_t wcid, PCMD pcmd);
#endif // 3DMMEx: KAUAI_WIN32

  protected:
    MUB(void)
    {
    }

  public:
    ~MUB(void);

    static PMUB PmubNew(uint32_t ridMenuBar);

    virtual void Set(void);
    virtual void Clean(void);

#ifdef MAC
    virtual bool FDoClick(EVT *pevt);
    virtual bool FDoKey(EVT *pevt);
#endif // 3DMMv1.0: MAC
#ifdef KAUAI_WIN32
    virtual void EnqueueWcid(int32_t wcid);
#endif // 3DMMEx: KAUAI_WIN32

    virtual bool FAddListCid(int32_t cid, uintptr_t lw0, PSTN pstn);
    virtual bool FRemoveListCid(int32_t cid, uintptr_t lw0, PSTN pstn = pvNil);
    virtual bool FChangeListCid(int32_t cid, uintptr_t lwOld, PSTN pstnOld, uintptr_t lwNew, PSTN pstnNew);
    virtual bool FRemoveAllListCid(int32_t cid);
};

extern PMUB vpmubCur;

#endif //! 3DMMv1.0: MENU_H
