/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Private audioman sound device header file.

***************************************************************************/
#ifndef SNDAMPRI_H
#define SNDAMPRI_H

/** 3DMMv1.0: *************************************************************************
    IStream interface for a BLCK.
***************************************************************************/
typedef class STBL *PSTBL;
#define STBL_PAR IStream
class STBL : public STBL_PAR
{
    ASSERT
    MARKMEM_BASE

  protected:
    int32_t _cactRef;
    int32_t _ib;
    BLCK _blck;

    STBL(void);
    ~STBL(void);

  public:
    // 3DMMv1.0: IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // 3DMMv1.0: IStream methods
    STDMETHODIMP Read(void *pv, ULONG cb, ULONG *pcb);
    STDMETHODIMP Write(VOID const *pv, ULONG cb, ULONG *pcb)
    {
        if (pvNil != pcb)
            *pcb = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition);
    STDMETHODIMP SetSize(ULARGE_INTEGER libNewSize)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP CopyTo(IStream *pStm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten)
    {
        if (pvNil != pcbRead)
            pcbRead->LowPart = pcbRead->HighPart = 0;
        if (pvNil != pcbWritten)
            pcbWritten->LowPart = pcbWritten->HighPart = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP Commit(DWORD grfCommitFlags)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP Revert(void)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP Stat(STATSTG *pstatstg, DWORD grfStatFlag)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP Clone(THIS_ IStream **ppstm)
    {
        *ppstm = pvNil;
        return E_NOTIMPL;
    }

    static PSTBL PstblNew(FLO *pflo, bool fPacked);
    int32_t CbMem(void)
    {
        return SIZEOF(STBL) + _blck.CbMem();
    }
    bool FInMemory(void)
    {
        return _blck.CbMem() > 0;
    }
};

/** 3DMMv1.0: *************************************************************************
    Cached AudioMan Sound.
***************************************************************************/
typedef class CAMS *PCAMS;
#define CAMS_PAR BACO
#define kclsCAMS KLCONST4('C', 'A', 'M', 'S')
class CAMS : public CAMS_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // 3DMMv1.0: this is just so we can do a MarkMemObj on it while AudioMan has it
    PSTBL _pstbl;

    CAMS(void);

  public:
    ~CAMS(void);
    static PCAMS PcamsNewLoop(PCAMS pcamsSrc, int32_t cactPlay);

    IAMSound *psnd; // 3DMMv1.0: the sound to use

    static bool FReadCams(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);
    bool FInMemory(void)
    {
        return _pstbl->FInMemory();
    }
};

/** 3DMMv1.0: *************************************************************************
    Notify sink class.
***************************************************************************/
typedef class AMQUE *PAMQUE; // 3DMMv1.0: forward declaration

typedef class AMNOT *PAMNOT;
#define AMNOT_PAR IAMNotifySink
class AMNOT : public AMNOT_PAR
{
    ASSERT

  protected:
    int32_t _cactRef;
    PAMQUE _pamque; // 3DMMv1.0: the amque to notify

  public:
    // 3DMMv1.0: IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // 3DMMv1.0: IAMNotifySink methods
    STDMETHODIMP_(void) OnStart(LPSOUND pSound, DWORD dwPosition)
    {
    }
    STDMETHODIMP_(void) OnCompletion(LPSOUND pSound, DWORD dwPosition);
    STDMETHODIMP_(void) OnError(LPSOUND pSound, DWORD dwPosition, HRESULT hrError)
    {
    }
    STDMETHODIMP_(void) OnSyncObject(LPSOUND pSound, DWORD dwPosition, void *pvObject)
    {
    }

    AMNOT(void);
    void Set(PAMQUE pamque);
};

/** 3DMMv1.0: *************************************************************************
    Audioman queue.
***************************************************************************/
#define AMQUE_PAR SNQUE
#define kclsAMQUE KLCONST4('a', 'm', 'q', 'u')
class AMQUE : public AMQUE_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    MUTX _mutx;         // 3DMMv1.0: restricts access to member variables
    IAMChannel *_pchan; // 3DMMv1.0: the audioman channel
    uint32_t _tsStart;  // 3DMMv1.0: when we started the current sound
    AMNOT _amnot;       // 3DMMv1.0: notify sink

    AMQUE(void);

    virtual void _Enter(void) override;
    virtual void _Leave(void) override;

    virtual bool _FInit(void) override;
    virtual PBACO _PbacoFetch(PRCA prca, CTG ctg, CNO cno) override;
    virtual void _Queue(int32_t isndinMin) override;
    virtual void _PauseQueue(int32_t isndinMin) override;
    virtual void _ResumeQueue(int32_t isndinMin) override;

  public:
    static PAMQUE PamqueNew(void);
    ~AMQUE(void);

    void Notify(LPSOUND psnd);
};

#endif //! 3DMMv1.0: SNDAMPRI_H
