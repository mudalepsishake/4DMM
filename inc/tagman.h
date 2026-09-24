/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: ***********************************************************************

    tagman.h: Tag Manager class (TAGM)

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> TAGM

    A TAG is a reference to a piece of content: a background, actor
    template, sound, etc.  In addition to a CTG and CNO, a TAG specifies
    a SID, or source ID, that helps TAGM find the content.

    A source (identified by a SID) is a group of chunky files (managed
    by a CRM) in one directory of one disk whose chunks all have unique
    CTG/CNOs.  Each Socrates series member will be a source, and the
    user rolls might also be implemented as a source.  A SID of less
    than 0 is invalid; a TAG with a negative SID is an invalid TAG.

    Each source has a name, managed by _pgstSource.  This name is used
    with the _pfninscd callback when the source cannot be found.  The
    callback should put up an alert saying (for example) "The source
    *Socrates* cannot be found...please insert the CD."

    TAGM supports caching chunks to the local hard disk.  In Socrates,
    the studio should call FCacheTagToHD as soon as the chunk is
    requested by the kid, and when the tag is resolved to a BACO, the
    HD copy is used.  This reduces headaches about dealing with a missing
    CD all over the place.

    If a tag has ksidUseCrf for its sid, the chunk is read from the tag's
    pcrf rather than a source or a source's cache.  Use this
    functionality for things like content chunks embedded in user
    documents.

*************************************************************************/
#ifndef TAGM_H
#define TAGM_H

const int32_t ksidInvalid = -1; // 3DMMv1.0: negative SIDs imply an invalid TAG
const int32_t sidNil = 0;
const int32_t ksidUseCrf = 0; // 3DMMv1.0: chunk is in ptag->pcrf

/* 3DMMEx: On-disk representation of TAG */
typedef struct TAGF *PTAGF;
struct TAGF
{
    int32_t sid;
    int32_t _pcrf;
    CTG ctg;
    CNO cno;
};
VERIFY_STRUCT_SIZE(TAGF, 16);

typedef struct TAG *PTAG;
struct TAG
{
#ifdef DEBUG
    // 3DMMv1.0: I can't use the MARKMEM macro because that makes MarkMem() virtual,
    // 3DMMv1.0: which changes size(TAG), which I don't want to do.
    void MarkMem(void);
#endif // 3DMMv1.0: DEBUG

    int32_t sid;   // 3DMMv1.0: Source ID (or ksidUseCrf)
    int32_t _pcrf; // 3DMMEx: was: pcrf
    CTG ctg;       // 3DMMv1.0: CTG of chunk
    CNO cno;       // 3DMMv1.0: CNO of chunk
    PCRF pcrf;     // 3DMMv1.0: File to look in for this chunk if sid is ksidUseCrf
};
const BOM kbomTag = 0xFF000000;

// 3DMMEx: Functions for serializing and deserializing tags
void DeserializeTagfToTag(PTAGF ptagf, PTAG ptag);
void SerializeTagToTagf(PTAG ptag, PTAGF ptagf);

// 3DMMv1.0: FNINSCD is a client-supplied callback function to alert the user to
// 3DMMv1.0: insert the given CD.  The name of the source is passed to the callback.
// 3DMMv1.0: The function should return fTrue if the user wants to retry searching
// 3DMMv1.0: for the chunk, or fFalse to cancel.
typedef bool FNINSCD(PSTN pstnSourceTitle);
typedef FNINSCD *PFNINSCD;

enum
{
    ftagmNil = 0x0000,
    ftagmFile = 0x0001,   // 3DMMv1.0: for ClearCache: clear HD cache
    ftagmMemory = 0x0002, // 3DMMv1.0: for ClearCache: clear CRF RAM cache
};

/** 3DMMv1.0: **************************************
    Tag Manager class
****************************************/
typedef class TAGM *PTAGM;
#define TAGM_PAR BASE
#define kclsTAGM KLCONST4('T', 'A', 'G', 'M')
class TAGM : public TAGM_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    FNI _fniHDRoot;     // 3DMMv1.0: Root HD directory to search for content
    int32_t _cbCache;   // 3DMMv1.0: Size of RAM Cache on files in CRM for each source
    PGL _pglsfs;        // 3DMMv1.0: GL of source file structs
    PGST _pgstSource;   // 3DMMv1.0: String table of source descriptions
    PFNINSCD _pfninscd; // 3DMMv1.0: Function to call when source is not found

  protected:
    TAGM(void)
    {
    }
    bool _FFindSid(int32_t sid, int32_t *pistn = pvNil);
    bool _FGetStnMergedOfSid(int32_t sid, PSTN pstn);
    bool _FGetStnSplitOfSid(int32_t sid, PSTN pstnLong, PSTN pstnShort);
    bool _FRetry(int32_t sid);
    bool _FEnsureFniCD(int32_t sid, PFNI pfniCD, PSTN pstn = pvNil);
    bool _FFindFniCD(int32_t sid, PFNI pfniCD, bool *pfFniChanged);
    bool _FDetermineIfSourceHD(int32_t sid, bool *pfSourceIsOnHD);
    bool _FDetermineIfContentOnFni(PFNI pfni, bool *pfContentOnFni);

    bool _FGetFniHD(int32_t sid, PFNI pfniHD);
    bool _FGetFniCD(int32_t sid, PFNI pfniHD, bool fAskForCD);

    bool _FBuildFniHD(int32_t sid, PFNI pfniHD, bool *pfExists);
    PCRM _PcrmSourceNew(int32_t sid, PFNI pfniInfo);
    PCRM _PcrmSourceGet(int32_t sid, bool fDontHitCD = fFalse);
    PCFL _PcflFindTag(PTAG ptag);

  public:
    static PTAGM PtagmNew(PFNI pfniHDRoot, PFNINSCD pfninscd, int32_t cbCache);
    ~TAGM(void);

    // 3DMMv1.0: GstSource stuff:
    PGST PgstSource(void);
    bool FMergeGstSource(PGST pgst, int16_t bo, int16_t osk);
    bool FAddStnSource(PSTN pstnMerged, int32_t sid);
    bool FSetStnSource(PSTN pstnMerged, int32_t sid);
    bool FRemoveStnSource(int32_t sid);
    bool FGetSid(PSTN pstn, int32_t *psid); // 3DMMv1.0: pstn can be short or long

    bool FFindFile(int32_t sid, PSTN pstn, PFNI pfni, bool fAskForCD);
    void SplitString(PSTN pstnMerged, PSTN pstnLong, PSTN pstnShort);

    bool FBuildChildTag(PTAG ptagPar, CHID chid, CTG ctgChild, PTAG ptagChild);
    // 4DMM Actor Studio needs to make a writable document-local copy of a
    // stock TMPL before adding user actions.  Return the already-managed CFL
    // that actually owns a tag; ownership stays with TAGM/CRM.
    PCFL PcflFindTag4DMM(PTAG ptag);
    bool FCacheTagToHD(PTAG ptag, bool fCacheChildChunks = fTrue);
    PBACO PbacoFetch(PTAG ptag, PFNRPO pfnrpo, bool fUseCD = fFalse);
    void ClearCache(int32_t sid = sidNil,
                    uint32_t grftagm = ftagmFile | ftagmMemory); // 3DMMv1.0: sidNil clears all caches

    // 3DMMv1.0: For ksidUseCrf tags:
    static bool FOpenTag(PTAG ptag, PCRF pcrfDest, PCFL pcflSrc = pvNil);
    static bool FSaveTag(PTAG ptag, PCRF pcrf, bool fRedirect);
    static void DupTag(PTAG ptag); // 3DMMv1.0: call this when you're copying a tag
    static void CloseTag(PTAG ptag);

    static uint32_t FcmpCompareTags(PTAG ptag1, PTAG ptag2);
};

#endif // 3DMMv1.0: TAGM_H
