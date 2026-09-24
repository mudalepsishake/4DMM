/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Dialog header file.

***************************************************************************/
#ifndef DLG_H
#define DLG_H

#ifdef MAC
typedef DialogPtr HDLG;
#endif // 3DMMv1.0: MAC
#ifdef WIN
typedef HWND HDLG;
#endif // 3DMMv1.0: WIN

// 3DMMv1.0: type of dialog item
enum
{
    ditkButton,     // 3DMMv1.0: no value
    ditkCheckBox,   // 3DMMv1.0: long (bool)
    ditkRadioGroup, // 3DMMv1.0: long (index)
    ditkEditText,   // 3DMMv1.0: streamed stn
    ditkCombo,      // 3DMMv1.0: streamed stn, followed by a list of streamed stn's.
    ditkLim
};

// 3DMMv1.0: dialog item
struct DIT
{
    int32_t sitMin; // 3DMMv1.0: first system item number (for this DIT)
    int32_t sitLim; // 3DMMv1.0: lim of system item numbers (for this DIT)
    int32_t ditk;   // 3DMMv1.0: kind of item
};

typedef class DLG *PDLG;

// 3DMMv1.0: callback to notify of an item change (while the dialog is active)
typedef bool (*PFNDLG)(PDLG pdlg, int32_t *pidit, void *pv);

// 3DMMv1.0: dialog class - a DLG is a GG of DITs
#define DLG_PAR GG
#define kclsDLG KLCONST3('D', 'L', 'G')
class DLG : public DLG_PAR
{
    RTCLASS_DEC

  private:
    PGOB _pgob;
    int32_t _rid;
    PFNDLG _pfn;
    void *_pv;

#ifdef WIN
    friend INT_PTR CALLBACK _FDlgCore(HWND hdlg, UINT msg, WPARAM w, LPARAM lw);
#endif // 3DMMv1.0: WIN

    DLG(int32_t rid);
    bool _FInit(void);

    int32_t _LwGetRadioGroup(int32_t idit);
    void _SetRadioGroup(int32_t idit, int32_t lw);
    bool _FGetCheckBox(int32_t idit);
    void _InvertCheckBox(int32_t idit);
    void _SetCheckBox(int32_t idit, bool fOn);
    void _GetEditText(int32_t idit, PSTN pstn);
    void _SetEditText(int32_t idit, PSTN pstn);
    bool _FDitChange(int32_t *pidit);
    bool _FAddToList(int32_t idit, PSTN pstn);
    void _ClearList(int32_t idit);

  public:
    static PDLG PdlgNew(int32_t rid, PFNDLG pfn = pvNil, void *pv = pvNil);

    int32_t IditDo(int32_t iditFocus = ivNil);

    // 3DMMv1.0: these are only valid while the dialog is up
    bool FGetValues(int32_t iditMin, int32_t iditLim);
    void SetValues(int32_t iditMin, int32_t iditLim);
    void SelectDit(int32_t idit);

    // 3DMMv1.0: argument access
    int32_t IditFromSit(int32_t sit);
    void GetDit(int32_t idit, DIT *pdit)
    {
        GetFixed(idit, pdit);
    }
    void PutDit(int32_t idit, DIT *pdit)
    {
        PutFixed(idit, pdit);
    }

    void GetStn(int32_t idit, PSTN pstn);
    bool FPutStn(int32_t idit, PSTN pstn);
    int32_t LwGetRadio(int32_t idit);
    void PutRadio(int32_t idit, int32_t lw);
    bool FGetCheck(int32_t idit);
    void PutCheck(int32_t idit, bool fOn);

    bool FGetLwFromEdit(int32_t idit, int32_t *plw, bool *pfEmpty = pvNil);
    bool FPutLwInEdit(int32_t idit, int32_t lw);

    bool FAddToList(int32_t idit, PSTN pstn);
    void ClearList(int32_t idit);
};

#endif //! 3DMMv1.0: DLG_H
