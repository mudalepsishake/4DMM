/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    modl.cpp: Model class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "soc.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <vector>
#if !defined(KAUAI_WIN32)
#include <sys/stat.h>
#endif
ASSERTNAME


RTCLASS(MODL)

static bool vfUvDump = fFalse;
static uint32_t vcUvDump = 0;
const int32_t kcchUvPath = 1024;

struct UVMODELINFO
{
    CTG ctg;
    CNO cno;
    uint32_t iDump;
    char szBase[kcchUvPath];
};

struct UVASSOCKEY
{
    const BMDL *pbmdl;
    const BMTL *pbmtl;
    const void *pvBody;
    int32_t ibact;
    int32_t ibset;

    bool operator<(const UVASSOCKEY &other) const
    {
        if (pbmdl != other.pbmdl)
            return pbmdl < other.pbmdl;
        if (pbmtl != other.pbmtl)
            return pbmtl < other.pbmtl;
        if (pvBody != other.pvBody)
            return pvBody < other.pvBody;
        if (ibact != other.ibact)
            return ibact < other.ibact;
        return ibset < other.ibset;
    }
};

static std::map<const BMDL *, UVMODELINFO> vmapUvModels;
static std::set<UVASSOCKEY> vsetUvAssociations;
static uint32_t vcUvAssociationDump = 0;

struct UVPOSKEY
{
    int64_t x;
    int64_t y;
    int64_t z;

    bool operator<(const UVPOSKEY &other) const
    {
        if (x != other.x)
            return x < other.x;
        if (y != other.y)
            return y < other.y;
        return z < other.z;
    }
};

static int64_t _UvQuantize(float value)
{
    return (int64_t)std::llround((double)value * 100000.0);
}

/***************************************************************************
    Rebuild prepared-vertex normals for -c from the actual prepared faces.

    The previous true-colour pass only averaged already-stored normals at
    duplicate positions.  That does nothing when a pre-prepared model has
    one shared vertex whose stored normal is itself coarse/quantised.  The
    old RGB888 rasterizer can interpolate full RGB between vertices, but it
    can only interpolate the lighting values BRender gives it.

    For every geometric position, gather the normals of every incident face
    (including faces using duplicate prepared vertices at that same point),
    then average the faces that lie within a 60-degree crease of the vertex's
    existing normal.  Curved surfaces therefore get a genuinely smooth
    normal field while cube/text hard edges remain split.
***************************************************************************/
static void _SmoothTrueColorPreparedNormals(PBMDL pbmdl)
{
    if (!BWLD::FTrueColorMode() || pbmdl == pvNil || pbmdl->prepared_vertices == pvNil ||
        pbmdl->prepared_faces == pvNil || pbmdl->nprepared_vertices <= 1 || pbmdl->nprepared_faces <= 0)
    {
        return;
    }

    struct POSKEY
    {
        BRS x;
        BRS y;
        BRS z;

        bool operator<(const POSKEY &other) const
        {
            if (x != other.x)
                return x < other.x;
            if (y != other.y)
                return y < other.y;
            return z < other.z;
        }
    };

    struct F3
    {
        float x;
        float y;
        float z;
    };

    typedef std::vector<int32_t> INDEXLIST;
    std::map<POSKEY, INDEXLIST> mapVertices;
    std::vector<INDEXLIST> rgFacesByVertex(pbmdl->nprepared_vertices);
    std::vector<F3> rgFaceNormals(pbmdl->nprepared_faces);
    std::vector<F3> rgFaceAreaNormals(pbmdl->nprepared_faces);

    for (int32_t ibrv = 0; ibrv < pbmdl->nprepared_vertices; ibrv++)
    {
        BRV *pbrv = &pbmdl->prepared_vertices[ibrv];
        POSKEY key = {pbrv->p.v[0], pbrv->p.v[1], pbrv->p.v[2]};
        mapVertices[key].push_back(ibrv);
    }

    // Build reliable float face normals from geometry instead of trusting
    // the pre-prepared normal field we are specifically trying to improve.
    for (int32_t ibrf = 0; ibrf < pbmdl->nprepared_faces; ibrf++)
    {
        BRF *pbrf = &pbmdl->prepared_faces[ibrf];
        const int32_t iv0 = pbrf->vertices[0];
        const int32_t iv1 = pbrf->vertices[1];
        const int32_t iv2 = pbrf->vertices[2];
        F3 fn = {0.0f, 0.0f, 0.0f};

        if (FIn(iv0, 0, pbmdl->nprepared_vertices) && FIn(iv1, 0, pbmdl->nprepared_vertices) &&
            FIn(iv2, 0, pbmdl->nprepared_vertices))
        {
            const BRV &v0 = pbmdl->prepared_vertices[iv0];
            const BRV &v1 = pbmdl->prepared_vertices[iv1];
            const BRV &v2 = pbmdl->prepared_vertices[iv2];
            const float ax = BrScalarToFloat(v1.p.v[0] - v0.p.v[0]);
            const float ay = BrScalarToFloat(v1.p.v[1] - v0.p.v[1]);
            const float az = BrScalarToFloat(v1.p.v[2] - v0.p.v[2]);
            const float bx = BrScalarToFloat(v2.p.v[0] - v0.p.v[0]);
            const float by = BrScalarToFloat(v2.p.v[1] - v0.p.v[1]);
            const float bz = BrScalarToFloat(v2.p.v[2] - v0.p.v[2]);
            fn.x = ay * bz - az * by;
            fn.y = az * bx - ax * bz;
            fn.z = ax * by - ay * bx;
            rgFaceAreaNormals[ibrf] = fn;

            const float len = std::sqrt(fn.x * fn.x + fn.y * fn.y + fn.z * fn.z);
            if (len > 0.000001f)
            {
                fn.x /= len;
                fn.y /= len;
                fn.z /= len;
            }
            else
            {
                fn.x = fn.y = fn.z = 0.0f;
                rgFaceAreaNormals[ibrf] = fn;
            }

            rgFacesByVertex[iv0].push_back(ibrf);
            rgFacesByVertex[iv1].push_back(ibrf);
            rgFacesByVertex[iv2].push_back(ibrf);
        }
        rgFaceNormals[ibrf] = fn;
    }

    // actorlight6: ordinary -c keeps the established 60-degree crease.
    // Under -a, prefer BRender's authored smoothing-group relationships at
    // coincident positions.  That is the engine's native mechanism for sharing
    // normals across UV/material splits without rounding genuinely hard edges.
    // Old models with no smoothing-group information fall back to the previous
    // 80-degree rule.
    const float kflCreaseDot = BWLD::FActorLightMode() ? 0.17364818f : 0.5f; // cos(80) / cos(60)
    const float kflFractionMax = 32767.0f / 32768.0f;

    for (std::map<POSKEY, INDEXLIST>::const_iterator it = mapVertices.begin(); it != mapVertices.end(); ++it)
    {
        const INDEXLIST &vertices = it->second;

        // Union all faces touching any prepared vertex at this exact
        // position.  This handles both shared-index and duplicated-index
        // pre-prepared meshes.
        INDEXLIST faces;
        for (size_t iv = 0; iv < vertices.size(); iv++)
        {
            const INDEXLIST &vf = rgFacesByVertex[vertices[iv]];
            faces.insert(faces.end(), vf.begin(), vf.end());
        }
        if (faces.empty())
            continue;
        std::sort(faces.begin(), faces.end());
        faces.erase(std::unique(faces.begin(), faces.end()), faces.end());

        for (size_t iv = 0; iv < vertices.size(); iv++)
        {
            const int32_t ibrv = vertices[iv];
            const br_fvector3 &normalOld = pbmdl->prepared_vertices[ibrv].n;
            float rx = (float)normalOld.v[0] / 32768.0f;
            float ry = (float)normalOld.v[1] / 32768.0f;
            float rz = (float)normalOld.v[2] / 32768.0f;
            float rlen = std::sqrt(rx * rx + ry * ry + rz * rz);

            // If the stored normal is unusable, seed with the first valid
            // incident face normal rather than abandoning smoothing.
            if (rlen > 0.000001f)
            {
                rx /= rlen;
                ry /= rlen;
                rz /= rlen;
            }
            else
            {
                for (size_t jf = 0; jf < faces.size(); jf++)
                {
                    const F3 &fn = rgFaceNormals[faces[jf]];
                    if (fn.x != 0.0f || fn.y != 0.0f || fn.z != 0.0f)
                    {
                        rx = fn.x;
                        ry = fn.y;
                        rz = fn.z;
                        break;
                    }
                }
            }

            float sx = 0.0f;
            float sy = 0.0f;
            float sz = 0.0f;
            int32_t cnormal = 0;

            uint32_t grfSmoothing = 0;
            if (BWLD::FActorLightMode())
            {
                const INDEXLIST &facesVertex = rgFacesByVertex[ibrv];
                for (size_t jf = 0; jf < facesVertex.size(); jf++)
                    grfSmoothing |= (uint32_t)pbmdl->prepared_faces[facesVertex[jf]].smoothing;
            }

            for (size_t jf = 0; jf < faces.size(); jf++)
            {
                const int32_t ibrf = faces[jf];
                const F3 &fn = rgFaceNormals[ibrf];
                if (fn.x == 0.0f && fn.y == 0.0f && fn.z == 0.0f)
                    continue;

                bool fShare = fFalse;
                if (BWLD::FActorLightMode() && grfSmoothing != 0)
                {
                    fShare = (((uint32_t)pbmdl->prepared_faces[ibrf].smoothing & grfSmoothing) != 0);
                }
                else
                {
                    const float dot = rx * fn.x + ry * fn.y + rz * fn.z;
                    fShare = (dot >= kflCreaseDot);
                }

                if (fShare)
                {
                    // Area-weighting stops tiny skinny triangles from having
                    // the same influence as a broad neighbouring surface.
                    const F3 &fan = rgFaceAreaNormals[ibrf];
                    sx += fan.x;
                    sy += fan.y;
                    sz += fan.z;
                    cnormal++;
                }
            }
            if (cnormal <= 0)
                continue;

            float len = std::sqrt(sx * sx + sy * sy + sz * sz);
            if (len <= 0.000001f)
                continue;
            sx /= len;
            sy /= len;
            sz /= len;

            if (sx > kflFractionMax) sx = kflFractionMax;
            if (sy > kflFractionMax) sy = kflFractionMax;
            if (sz > kflFractionMax) sz = kflFractionMax;
            if (sx < -1.0f) sx = -1.0f;
            if (sy < -1.0f) sy = -1.0f;
            if (sz < -1.0f) sz = -1.0f;

            BRV *pbrv = &pbmdl->prepared_vertices[ibrv];
            pbrv->n.v[0] = (br_fraction)std::lround(sx * 32768.0f);
            pbrv->n.v[1] = (br_fraction)std::lround(sy * 32768.0f);
            pbrv->n.v[2] = (br_fraction)std::lround(sz * 32768.0f);
        }
    }
}

static bool _FGetUvDumpDir(char *szDumpDir)
{
#if defined(KAUAI_WIN32)
    char szExe[kcchUvPath];
    char szDir[kcchUvPath];

    if (!GetModuleFileNameA(NULL, szExe, SIZEOF(szExe)))
        return fFalse;

    CopyPb(szExe, szDir, SIZEOF(szExe));
    int32_t ichMac = 0;
    while (ichMac < kcchUvPath && szDir[ichMac] != '\0')
        ichMac++;
    for (int32_t ich = ichMac - 1; ich >= 0; ich--)
    {
        if (szDir[ich] == '\\' || szDir[ich] == '/')
        {
            szDir[ich] = '\0';
            break;
        }
    }

    wsprintfA(szDumpDir, "%s\\uv_dumps", szDir);
    if (!CreateDirectoryA(szDumpDir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return fFalse;
#else
    CopyPb("uv_dumps", szDumpDir, 9);
    szDumpDir[8] = '\0';
    mkdir(szDumpDir, 0755);
#endif
    return fTrue;
}

void MODL::SetUvDumpEnabled(bool fEnabled)
{
    vfUvDump = FPure(fEnabled);
}

/***************************************************************************
    Returns whether -uvdump diagnostics are active.
***************************************************************************/
bool MODL::FUvDumpEnabled(void)
{
    return vfUvDump;
}

/***************************************************************************
    Return whether a live BRender model came from a specific source chunk.
    The UV diagnostic already records this exact association when the model
    is loaded, which is more reliable than re-identifying a model later from
    mutable BRender state.
***************************************************************************/
bool MODL::FSourceChunk(PBMDL pbmdl, CTG ctg, CNO cno)
{
    std::map<const BMDL *, UVMODELINFO>::const_iterator it = vmapUvModels.find(pbmdl);
    return it != vmapUvModels.end() && it->second.ctg == ctg && it->second.cno == cno;
}

/** 3DMMv1.0: *************************************************************************
    Create a new PMODL based on some vertices and faces.
***************************************************************************/
PMODL MODL::PmodlNew(int32_t cbrv, BRV *prgbrv, int32_t cbrf, BRF *prgbrf)
{
    AssertIn(cbrv, 0, ksuMax); // 3DMMv1.0: ushort in br_model
    AssertPvCb(prgbrv, LwMul(cbrv, SIZEOF(BRV)));
    AssertIn(cbrf, 0, ksuMax); // 3DMMv1.0: ushort in br_model
    AssertPvCb(prgbrf, LwMul(cbrf, SIZEOF(BRF)));

    PMODL pmodl;
    char szIdentifier[SIZEOF(PMODL) + 1];

    pmodl = NewObj MODL;
    if (pvNil == pmodl)
        goto LFail;
    ClearPb(szIdentifier, SIZEOF(PMODL) + 1);
#if defined(BRENDER_MODERN_14)
    BrModernLog("MODL::PmodlNew BrModelAllocate BEGIN cver=%ld cfac=%ld classes groups=%p prep_v=%p prep_f=%p",
                (long)cbrv, (long)cbrf, BrResClassFind("GROUPS"),
                BrResClassFind("PREPARED_VERTICES"), BrResClassFind("PREPARED_FACES"));
#endif
    pmodl->_pbmdl = BrModelAllocate(szIdentifier, cbrv, cbrf);
#if defined(BRENDER_MODERN_14)
    BrModernLog("MODL::PmodlNew BrModelAllocate RETURN model=%p", pmodl->_pbmdl);
#endif
    if (pvNil == pmodl->_pbmdl)
        goto LFail;
    CopyPb(&pmodl, pmodl->_pbmdl->identifier, SIZEOF(PMODL));
    CopyPb(prgbrv, pmodl->_pbmdl->vertices, LwMul(cbrv, SIZEOF(BRV)));
    CopyPb(prgbrf, pmodl->_pbmdl->faces, LwMul(cbrf, SIZEOF(BRF)));
    BrModelAdd(pmodl->_pbmdl);
    AssertPo(pmodl, 0);
    return pmodl;
LFail:
    ReleasePpo(&pmodl);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    A PFNRPO to read a MODL from a file
***************************************************************************/
bool MODL::FReadModl(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    MODL *pmodl;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;

    uint64_t qwPerfRead = MVIE::FPerformanceMode() ? MVIE::PerfNow() : 0;

    if (!pblck->FUnpackData())
        goto LFail;
    *pcb = pblck->Cb();

    pmodl = NewObj MODL;
    if (pvNil == pmodl || !pmodl->_FInit(pblck))
    {
        ReleasePpo(&pmodl);
    LFail:
        TrashVar(ppbaco);
        TrashVar(pcb);
        if (MVIE::FPerformanceMode())
            MVIE::PerfRecordModelRead(MVIE::PerfElapsedUs(qwPerfRead), ctg, cno, fFalse);
        return pvNil;
    }
    AssertPo(pmodl, 0);

    if (vfUvDump)
        pmodl->_DumpUv(ctg, cno);

    *ppbaco = pmodl;
    if (MVIE::FPerformanceMode())
    {
        bool fPreprepared = pmodl->Pbmdl() != pvNil &&
                            FPure(pmodl->Pbmdl()->flags & BR_MODF_PREPREPARED);
        MVIE::PerfRecordModelRead(MVIE::PerfElapsedUs(qwPerfRead), ctg, cno, fPreprepared);
    }
    return fTrue;
}

#if defined(BRENDER_MODERN_14)
/***************************************************************************
    Read 3DMM's fixed 1995 br_face_file records into BRender 1.4 faces.
    The modern br_face is intentionally a different runtime structure, so
    serialized 32-byte face records must never be read directly into it.
***************************************************************************/
static bool _FReadLegacyFaces(PBLCK pblck, int32_t ib, int16_t bo, BRF *prgbrf, int32_t cbrf)
{
    if (cbrf <= 0)
        return fTrue;

    std::vector<BRFF> rgbrff(cbrf);
    if (!pblck->FReadRgb(rgbrff.data(), LwMul(cbrf, SIZEOF(BRFF)), ib))
        return fFalse;

    for (int32_t ibrf = 0; ibrf < cbrf; ++ibrf)
    {
        BRFF &brff = rgbrff[ibrf];
        if (kboOther == bo)
            SwapBytesBom(&brff, kbomBrf);

        BRF &brf = prgbrf[ibrf];
        ClearPb(&brf, SIZEOF(brf));
        brf.vertices[0] = brff.vertices[0];
        brf.vertices[1] = brff.vertices[1];
        brf.vertices[2] = brff.vertices[2];
        brf.material = pvNil;
        brf.smoothing = brff.smoothing;
        brf.flags = brff.flags;
        brf.index = 0;
        brf.red = 255;
        brf.grn = 255;
        brf.blu = 255;
        brf.n = brff.n;
        brf.d = brff.d;
    }

    return fTrue;
}
#endif

/** 3DMMEx: *************************************************************************
    Deserialize BMDL from on-disk format
***************************************************************************/
bool DeserializeBMDL(int16_t bo, PBMDL pbmdl)
{
    int32_t ibrv, ibrf;
    BRV *pbrv;
    BRF *pbrf;
    BRFF *pfaces, *pbrff;

    if (pbmdl->nprepared_vertices)
    {
        if (kboOther == bo)
        {
            for (ibrv = 0, pbrv = pbmdl->prepared_vertices; ibrv < pbmdl->nprepared_vertices; ibrv++, pbrv++)
            {
                SwapBytesBom(pbrv, kbomBrv);
            }
        }
    }

#if defined(BRENDER_MODERN_14)
    // Modern faces were already converted from BRFF by _FReadLegacyFaces().
    return fTrue;
#else
    if (pbmdl->nprepared_faces)
    {
        if (!FAllocPv((void **)&pfaces, pbmdl->nprepared_faces * SIZEOF(BRFF), fmemClear, mprNormal))
            return fFalse;
        CopyPb(pbmdl->prepared_faces, pfaces, pbmdl->nprepared_faces * SIZEOF(BRFF));

        if (kboOther == bo)
        {
            for (ibrf = 0, pbrff = pfaces; ibrf < pbmdl->nprepared_faces; ibrf++, pbrff++)
            {
                SwapBytesBom(pbrff, kbomBrf);
            }
        }

        for (ibrf = 0, pbrff = pfaces, pbrf = pbmdl->prepared_faces; ibrf < pbmdl->nprepared_faces;
             ibrf++, pbrff++, pbrf++)
        {
            for (int i = 0; i < 3; i++)
            {
                pbrf->vertices[i] = pbrff->vertices[i];
                pbrf->edges[i] = pbrff->edges[i];
            }

            pbrf->material = pvNil;
            pbrf->smoothing = pbrff->smoothing;
            pbrf->flags = pbrff->flags;
            pbrf->n = pbrff->n;
            pbrf->d = pbrff->d;
        }

        FreePpv((void **)&pfaces);
    }
    else
    {
        if (!FAllocPv((void **)&pfaces, pbmdl->nfaces * SIZEOF(BRFF), fmemClear, mprNormal))
            return fFalse;
        CopyPb(pbmdl->faces, pfaces, pbmdl->nfaces * SIZEOF(BRFF));

        for (ibrf = 0, pbrff = pfaces, pbrf = pbmdl->faces; ibrf < pbmdl->nfaces; ibrf++, pbrff++, pbrf++)
        {
            for (int i = 0; i < 3; i++)
            {
                pbrf->vertices[i] = pbrff->vertices[i];
                pbrf->edges[i] = pbrff->edges[i];
            }

            pbrf->material = pvNil;
            pbrf->smoothing = pbrff->smoothing;
            pbrf->flags = pbrff->flags;
            pbrf->n = pbrff->n;
            pbrf->d = pbrff->d;
        }

        FreePpv((void **)&pfaces);
    }

    return fTrue;
#endif
}

/***************************************************************************
    Compute two independent 64-bit fingerprints over the unpacked serialized
    MODL chunk. This is deliberately source-data identity, not prepared
    BRender state, which may contain actor-local lighting/preparation data.
***************************************************************************/
static bool _FModelSourceFingerprint(PBLCK pblck, int32_t *pcb, uint64_t *pqwHashA, uint64_t *pqwHashB)
{
    AssertPo(pblck, 0);
    AssertVarMem(pcb);
    AssertVarMem(pqwHashA);
    AssertVarMem(pqwHashB);

    int32_t cb = pblck->Cb();
    uint64_t qwA = 14695981039346656037ULL;
    uint64_t qwB = 5381ULL;
    uint8_t rgb[4096];

    for (int32_t ib = 0; ib < cb; ib += SIZEOF(rgb))
    {
        int32_t cbRead = (cb - ib < (int32_t)SIZEOF(rgb)) ? cb - ib : (int32_t)SIZEOF(rgb);
        if (!pblck->FReadRgb(rgb, cbRead, ib))
            return fFalse;
        for (int32_t i = 0; i < cbRead; i++)
        {
            qwA ^= (uint64_t)rgb[i];
            qwA *= 1099511628211ULL;
            qwB = ((qwB << 5) + qwB) ^ (uint64_t)rgb[i];
        }
    }

    *pcb = cb;
    *pqwHashA = qwA;
    *pqwHashB = qwB;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Reads a MODL from a BLCK
***************************************************************************/
bool MODL::_FInit(PBLCK pblck)
{
    AssertBaseThis(0);
    AssertPo(pblck, 0);
#if defined(BRENDER_MODERN_14)
    BrModernLog("MODL::_FInit BEGIN this=%p block=%p cb=%ld", this, pblck, (long)pblck->Cb());
#endif

    MODLF modlf;
    int32_t cbrgbrv;
    int32_t cbrgbrf;
    int32_t ibrv;
    BRV *pbrv;
    int32_t ibrf;
    BRF *pbrf;
    MODL *pmodlThis = this;
    char szIdentifier[SIZEOF(PMODL) + 1];

    ClearPb(szIdentifier, SIZEOF(PMODL) + 1);
    if (!pblck->FUnpackData())
        return fFalse;

    // actorlight24: capture source identity once, while the model chunk is
    // already unpacked for loading. Failure only disables the optimization;
    // it must never make an otherwise valid model fail to load.
    _fSourceFingerprint = _FModelSourceFingerprint(pblck, &_cbSourceFingerprint,
                                                    &_qwSourceHashA, &_qwSourceHashB);
    if (!_fSourceFingerprint)
    {
        _cbSourceFingerprint = 0;
        _qwSourceHashA = 0;
        _qwSourceHashB = 0;
    }

    if (pblck->Cb() < SIZEOF(MODLF))
        return fFalse;
    if (!pblck->FReadRgb(&modlf, SIZEOF(MODLF), 0))
        return fFalse;
    if (kboOther == modlf.bo)
        SwapBytesBom(&modlf, kbomModlf);
    Assert(kboCur == modlf.bo, "bad MODL!");
#if defined(BRENDER_MODERN_14)
    BrModernLog("MODL::_FInit header cver=%u cfac=%u radius=%.6f prepared=%d",
                (unsigned)modlf.cver, (unsigned)modlf.cfac,
                (double)BrScalarToFloat(modlf.rRadius), (int)(modlf.rRadius != rZero));
#endif

    // 3DMMv1.0: Allocate space for the BMDL, array of vertices, and array of faces
    cbrgbrv = LwMul(modlf.cver, SIZEOF(BRV));
    cbrgbrf = LwMul(modlf.cfac, SIZEOF(BRFF));

    if (modlf.rRadius == rZero)
    {
        // 3DMMv1.0: unprepared model.  Gotta prepare it.

#if defined(BRENDER_MODERN_14)
        BrModernLog("MODL::_FInit BrModelAllocate BEGIN kind=unprepared cver=%u cfac=%u classes groups=%p prep_v=%p prep_f=%p",
                    (unsigned)modlf.cver, (unsigned)modlf.cfac, BrResClassFind("GROUPS"),
                    BrResClassFind("PREPARED_VERTICES"), BrResClassFind("PREPARED_FACES"));
#endif
        _pbmdl = BrModelAllocate(szIdentifier, modlf.cver, modlf.cfac);
#if defined(BRENDER_MODERN_14)
        BrModernLog("MODL::_FInit BrModelAllocate RETURN kind=unprepared model=%p", _pbmdl);
#endif
        if (pvNil == _pbmdl)
            return fFalse;
#if defined(BRENDER_MODERN_14)
        // 3DMM continues to read and mutate the public model arrays after
        // BrModelAdd().  BRender 1.4 otherwise frees those arrays while
        // preparing a non-updateable model, leaving our legacy aliases dangling.
        _pbmdl->flags |= BR_MODF_UPDATEABLE;
        BrModernLog("MODL::_FInit allocated unprepared model=%p verts=%p faces=%p flags=0x%04X",
                    _pbmdl, _pbmdl->vertices, _pbmdl->faces, (unsigned)_pbmdl->flags);
#endif
        CopyPb(&pmodlThis, _pbmdl->identifier, SIZEOF(PMODL));
        if (!pblck->FReadRgb(_pbmdl->vertices, cbrgbrv, SIZEOF(MODLF)))
            return fFalse;
#if defined(BRENDER_MODERN_14)
        if (!_FReadLegacyFaces(pblck, SIZEOF(MODLF) + cbrgbrv, modlf.bo,
                               _pbmdl->faces, modlf.cfac))
            return fFalse;
#else
        if (!pblck->FReadRgb(_pbmdl->faces, cbrgbrf, SIZEOF(MODLF) + cbrgbrv))
            return fFalse;
#endif

        if (!DeserializeBMDL(modlf.bo, _pbmdl))
            return fFalse;

#if defined(BRENDER_MODERN_14)
        // In 1.4 the app-visible prepared arrays are a compatibility view of
        // the retained source mesh.  3DMM's BODY/MODL code still consumes
        // these fields directly, while BRender 1.4 owns model->prepared.
        _pbmdl->prepared_vertices = _pbmdl->vertices;
        _pbmdl->prepared_faces = _pbmdl->faces;
        _pbmdl->nprepared_vertices = _pbmdl->nvertices;
        _pbmdl->nprepared_faces = _pbmdl->nfaces;
        _pbmdl->nvertex_groups = 1;
        _pbmdl->nface_groups = 1;
        if (_pbmdl->vertex_groups == pvNil)
            _pbmdl->vertex_groups = (br_vertex_group *)BrResAllocate(_pbmdl, SIZEOF(br_vertex_group), BR_MEMORY_GROUPS);
        if (_pbmdl->face_groups == pvNil)
            _pbmdl->face_groups = (br_face_group *)BrResAllocate(_pbmdl, SIZEOF(br_face_group), BR_MEMORY_GROUPS);
        if (_pbmdl->vertex_groups == pvNil || _pbmdl->face_groups == pvNil)
        {
            BrModernLog("MODL::_FInit FAIL unprepared legacy group allocation");
            return fFalse;
        }
        _pbmdl->vertex_groups->material = pvNil;
        _pbmdl->vertex_groups->vertices = _pbmdl->prepared_vertices;
        _pbmdl->vertex_groups->nvertices = _pbmdl->nprepared_vertices;
        _pbmdl->face_groups->material = pvNil;
        _pbmdl->face_groups->faces = _pbmdl->prepared_faces;
        _pbmdl->face_groups->nfaces = _pbmdl->nprepared_faces;
        _SmoothTrueColorPreparedNormals(_pbmdl);
        BrModernLog("MODL::_FInit BrModelAdd BEGIN model=%p nvertices=%u nfaces=%u flags=0x%04X",
                    _pbmdl, (unsigned)_pbmdl->nvertices, (unsigned)_pbmdl->nfaces, (unsigned)_pbmdl->flags);
        BrModelAdd(_pbmdl);
        BrModernLog("MODL::_FInit BrModelAdd returned model=%p verts=%p faces=%p prepared=%p stored=%p legacy_pv=%p legacy_pf=%p",
                    _pbmdl, _pbmdl->vertices, _pbmdl->faces, _pbmdl->prepared, _pbmdl->stored,
                    _pbmdl->prepared_vertices, _pbmdl->prepared_faces);
#else
        BrModelAdd(_pbmdl);
        _SmoothTrueColorPreparedNormals(_pbmdl);
#endif

        // 3DMMv1.0: REVIEW *****: uncomment and expand the following code to prelight models
        // 3DMMv1.0:		BVEC3 bvec3;
        // 3DMMv1.0:		if (!_FPrelight(1, &bvec3))
        // 3DMMv1.0:			return fFalse;
    }
    else
    {
        // pre-prepared model. Modern BRender cannot consume the 1995 private
        // prepared representation directly. In the 1.4 compatibility build,
        // preserve that serialized mesh as the retained source mesh and let
        // 1.4 build its own private prepared representation from it.
#if defined(BRENDER_MODERN_14)
        BrModernLog("MODL::_FInit BrModelAllocate BEGIN kind=preprepared cver=%u cfac=%u classes groups=%p prep_v=%p prep_f=%p",
                    (unsigned)modlf.cver, (unsigned)modlf.cfac, BrResClassFind("GROUPS"),
                    BrResClassFind("PREPARED_VERTICES"), BrResClassFind("PREPARED_FACES"));
        _pbmdl = BrModelAllocate(szIdentifier, modlf.cver, modlf.cfac);
        BrModernLog("MODL::_FInit BrModelAllocate RETURN kind=preprepared model=%p", _pbmdl);
#else
        _pbmdl = BrModelAllocate(szIdentifier, 0, 0);
#endif
        if (pvNil == _pbmdl)
            return fFalse;
        CopyPb(&pmodlThis, _pbmdl->identifier, SIZEOF(PMODL));

#if defined(BRENDER_MODERN_14)
        _pbmdl->prepared_vertices = _pbmdl->vertices;
        _pbmdl->prepared_faces = _pbmdl->faces;
#else
        _pbmdl->prepared_vertices =
            (BRV *)BrResAllocate(_pbmdl, LwMul(modlf.cver, SIZEOF(BRV)), BR_MEMORY_PREPARED_VERTICES);
        if (pvNil == _pbmdl->prepared_vertices)
            return fFalse;
        _pbmdl->prepared_faces = (BRF *)BrResAllocate(_pbmdl, LwMul(modlf.cfac, SIZEOF(BRF)), BR_MEMORY_PREPARED_FACES);
        if (pvNil == _pbmdl->prepared_faces)
            return fFalse;
#endif

        if (!pblck->FReadRgb(_pbmdl->prepared_vertices, cbrgbrv, SIZEOF(MODLF)))
        {
            return fFalse;
        }

#if defined(BRENDER_MODERN_14)
        if (!_FReadLegacyFaces(pblck, SIZEOF(MODLF) + cbrgbrv, modlf.bo,
                               _pbmdl->prepared_faces, modlf.cfac))
        {
            return fFalse;
        }
#else
        if (!pblck->FReadRgb(_pbmdl->prepared_faces, cbrgbrf, SIZEOF(MODLF) + cbrgbrv))
        {
            return fFalse;
        }
#endif

#if defined(BRENDER_MODERN_14)
        _pbmdl->flags = BR_MODF_DONT_WELD | BR_MODF_CUSTOM_NORMALS | BR_MODF_CUSTOM_EQUATIONS |
                        BR_MODF_CUSTOM_BOUNDS | BR_MODF_UPDATEABLE;
        BrModernLog("MODL::_FInit preprepared retained-source flags=0x%04X model=%p verts=%p faces=%p",
                    (unsigned)_pbmdl->flags, _pbmdl, _pbmdl->vertices, _pbmdl->faces);
#else
        _pbmdl->flags = BR_MODF_PREPREPARED;
#endif
        _pbmdl->nprepared_vertices = (uint16_t)modlf.cver;
        _pbmdl->nprepared_faces = (uint16_t)modlf.cfac;

        if (!DeserializeBMDL(modlf.bo, _pbmdl))
            return fFalse;

        // 3DMMv1.0: The following code assumes that there is no material data
        // 3DMMv1.0: in the models.  If there is material data, the code will have
        // 3DMMv1.0: to change to read vertex groups and face groups from file.
        _pbmdl->nvertex_groups = 1;
        _pbmdl->nface_groups = 1;
#if !defined(BRENDER_MODERN_14)
        _pbmdl->vertex_groups = (br_vertex_group *)BrResAllocate(_pbmdl, SIZEOF(br_vertex_group), BR_MEMORY_GROUPS);
        if (pvNil == _pbmdl->vertex_groups)
            return fFalse;
        _pbmdl->face_groups = (br_face_group *)BrResAllocate(_pbmdl, SIZEOF(br_face_group), BR_MEMORY_GROUPS);
        if (pvNil == _pbmdl->face_groups)
            return fFalse;
#else
        // These groups are a 3DMM compatibility view, not BRender 1.4's
        // private v11 groups.  Allocate them here if the compatibility
        // allocator did not, rather than failing every pre-prepared MODL.
        if (pvNil == _pbmdl->vertex_groups)
            _pbmdl->vertex_groups = (br_vertex_group *)BrResAllocate(_pbmdl, SIZEOF(br_vertex_group), BR_MEMORY_GROUPS);
        if (pvNil == _pbmdl->face_groups)
            _pbmdl->face_groups = (br_face_group *)BrResAllocate(_pbmdl, SIZEOF(br_face_group), BR_MEMORY_GROUPS);
        BrModernLog("MODL::_FInit legacy groups vertex=%p face=%p", _pbmdl->vertex_groups, _pbmdl->face_groups);
#endif
        if (pvNil == _pbmdl->vertex_groups || pvNil == _pbmdl->face_groups)
        {
#if defined(BRENDER_MODERN_14)
            BrModernLog("MODL::_FInit FAIL legacy group allocation");
#endif
            return fFalse;
        }
        _pbmdl->vertex_groups->material = pvNil;
        _pbmdl->vertex_groups->vertices = _pbmdl->prepared_vertices;
        _pbmdl->vertex_groups->nvertices = _pbmdl->nprepared_vertices;
        _pbmdl->face_groups->material = pvNil;
        _pbmdl->face_groups->faces = _pbmdl->prepared_faces;
        _pbmdl->face_groups->nfaces = _pbmdl->nprepared_faces;
        _pbmdl->radius = modlf.rRadius;
        _pbmdl->bounds = modlf.brb;
        _pbmdl->pivot = modlf.bvec3Pivot;
        _SmoothTrueColorPreparedNormals(_pbmdl);
#if defined(BRENDER_MODERN_14)
        BrModernLog("MODL::_FInit preprepared BrModelAdd BEGIN model=%p nvertices=%u nfaces=%u flags=0x%04X",
                    _pbmdl, (unsigned)_pbmdl->nvertices, (unsigned)_pbmdl->nfaces, (unsigned)_pbmdl->flags);
#endif
        BrModelAdd(_pbmdl);
#if defined(BRENDER_MODERN_14)
        BrModernLog("MODL::_FInit preprepared BrModelAdd returned model=%p verts=%p faces=%p prepared=%p stored=%p legacy_pv=%p legacy_pf=%p",
                    _pbmdl, _pbmdl->vertices, _pbmdl->faces, _pbmdl->prepared, _pbmdl->stored,
                    _pbmdl->prepared_vertices, _pbmdl->prepared_faces);
#endif
    }
#if defined(BRENDER_MODERN_14)
    BrModernLog("MODL::_FInit SUCCESS this=%p model=%p", this, _pbmdl);
#endif
    return fTrue;
}


/***************************************************************************
    Export the exact BRender model vertices, UVs, and faces used by the
    renderer.  This is diagnostic-only and is enabled with -uvdump.
***************************************************************************/
void MODL::_DumpUv(CTG ctg, CNO cno)
{
    AssertBaseThis(0);

    if (!vfUvDump || _pbmdl == pvNil)
        return;

    const BRV *prgbrv = _pbmdl->prepared_vertices;
    const BRF *prgbrf = _pbmdl->prepared_faces;
    int32_t cbrv = _pbmdl->nprepared_vertices;
    int32_t cbrf = _pbmdl->nprepared_faces;
    PCSZ pszSource = PszLit("prepared");

    if (prgbrv == pvNil || prgbrf == pvNil || cbrv <= 0 || cbrf <= 0)
    {
        prgbrv = _pbmdl->vertices;
        prgbrf = _pbmdl->faces;
        cbrv = _pbmdl->nvertices;
        cbrf = _pbmdl->nfaces;
        pszSource = PszLit("original");
    }

    if (prgbrv == pvNil || prgbrf == pvNil || cbrv <= 0 || cbrf <= 0)
        return;

    char szDumpDir[kcchUvPath];
    if (!_FGetUvDumpDir(szDumpDir))
        return;

    uint32_t iDump = ++vcUvDump;
    char szBase[kcchUvPath];
    char szObj[kcchUvPath];
    char szCsv[kcchUvPath];
    char szReport[kcchUvPath];
    char szIndex[kcchUvPath];

#if defined(KAUAI_WIN32)
    wsprintfA(szBase, "MODL_%08X_%08X_%06u", (uint32_t)ctg, (uint32_t)cno, iDump);
    wsprintfA(szObj, "%s\\%s.obj", szDumpDir, szBase);
    wsprintfA(szCsv, "%s\\%s.csv", szDumpDir, szBase);
    wsprintfA(szReport, "%s\\%s_uv_report.txt", szDumpDir, szBase);
    wsprintfA(szIndex, "%s\\index.csv", szDumpDir);
#else
    snprintf(szBase, SIZEOF(szBase), "MODL_%08X_%08X_%06u", (uint32_t)ctg, (uint32_t)cno, iDump);
    snprintf(szObj, SIZEOF(szObj), "%s/%s.obj", szDumpDir, szBase);
    snprintf(szCsv, SIZEOF(szCsv), "%s/%s.csv", szDumpDir, szBase);
    snprintf(szReport, SIZEOF(szReport), "%s/%s_uv_report.txt", szDumpDir, szBase);
    snprintf(szIndex, SIZEOF(szIndex), "%s/index.csv", szDumpDir);
#endif

    UVMODELINFO info;
    info.ctg = ctg;
    info.cno = cno;
    info.iDump = iDump;
    ClearPb(info.szBase, SIZEOF(info.szBase));
    CopyPb(szBase, info.szBase, LwMin(CchSz(szBase) + 1, SIZEOF(info.szBase)));
    vmapUvModels[_pbmdl] = info;

    std::ofstream obj(szObj, std::ios::out | std::ios::trunc);
    std::ofstream csv(szCsv, std::ios::out | std::ios::trunc);
    std::ofstream report(szReport, std::ios::out | std::ios::trunc);
    if (!obj || !csv || !report)
        return;

    obj << "# 3DMMEx -uvdump\n";
    obj << "# ctg=0x" << std::hex << std::uppercase << (uint32_t)ctg;
    obj << " cno=0x" << (uint32_t)cno << std::dec;
    obj << " source=" << pszSource << " vertices=" << cbrv << " faces=" << cbrf << "\n";
    obj << std::setprecision(9);

    csv << "record,index,x,y,z,u,v,v0,v1,v2,smoothing,flags\n";
    csv << std::setprecision(9);

    std::vector<std::vector<int32_t>> rgFacesByVertex((size_t)cbrv);
    std::map<UVPOSKEY, std::vector<int32_t>> mapPositions;

    for (int32_t ibrv = 0; ibrv < cbrv; ibrv++)
    {
        float x = BrScalarToFloat(prgbrv[ibrv].p.v[0]);
        float y = BrScalarToFloat(prgbrv[ibrv].p.v[1]);
        float z = BrScalarToFloat(prgbrv[ibrv].p.v[2]);
        float u = BrScalarToFloat(prgbrv[ibrv].map.v[0]);
        float v = BrScalarToFloat(prgbrv[ibrv].map.v[1]);

        obj << "v " << x << " " << y << " " << z << "\n";
        csv << "vertex," << ibrv << "," << x << "," << y << "," << z << "," << u << "," << v
            << ",,,,,\n";

        UVPOSKEY key = {_UvQuantize(x), _UvQuantize(y), _UvQuantize(z)};
        mapPositions[key].push_back(ibrv);
    }

    for (int32_t ibrv = 0; ibrv < cbrv; ibrv++)
    {
        float u = BrScalarToFloat(prgbrv[ibrv].map.v[0]);
        float v = BrScalarToFloat(prgbrv[ibrv].map.v[1]);
        obj << "vt " << u << " " << v << "\n";
    }

    for (int32_t ibrf = 0; ibrf < cbrf; ibrf++)
    {
        int32_t iv0 = prgbrf[ibrf].vertices[0];
        int32_t iv1 = prgbrf[ibrf].vertices[1];
        int32_t iv2 = prgbrf[ibrf].vertices[2];
        if (iv0 < 0 || iv0 >= cbrv || iv1 < 0 || iv1 >= cbrv || iv2 < 0 || iv2 >= cbrv)
            continue;

        obj << "f " << iv0 + 1 << "/" << iv0 + 1 << " " << iv1 + 1 << "/" << iv1 + 1 << " " << iv2 + 1
            << "/" << iv2 + 1 << "\n";
        csv << "face," << ibrf << ",,,,,," << iv0 << "," << iv1 << "," << iv2 << ","
            << prgbrf[ibrf].smoothing << "," << (uint32_t)prgbrf[ibrf].flags << "\n";

        rgFacesByVertex[(size_t)iv0].push_back(ibrf);
        rgFacesByVertex[(size_t)iv1].push_back(ibrf);
        rgFacesByVertex[(size_t)iv2].push_back(ibrf);
    }

    int32_t cGroups = 0;
    int32_t cDisagreements = 0;
    report << "3DMMEx UV duplicate-position report\n";
    report << "ctg=0x" << std::hex << std::uppercase << (uint32_t)ctg << " cno=0x" << (uint32_t)cno << std::dec
           << " source=" << pszSource << "\n";
    report << "Position epsilon: 0.00001 model units\n";
    report << "UV disagreement epsilon: 0.00001\n\n";
    report << std::setprecision(9);

    for (const auto &entry : mapPositions)
    {
        const std::vector<int32_t> &vertices = entry.second;
        if (vertices.size() < 2)
            continue;

        cGroups++;
        float uMin = BrScalarToFloat(prgbrv[vertices[0]].map.v[0]);
        float uMax = uMin;
        float vMin = BrScalarToFloat(prgbrv[vertices[0]].map.v[1]);
        float vMax = vMin;
        for (int32_t ibrv : vertices)
        {
            float u = BrScalarToFloat(prgbrv[ibrv].map.v[0]);
            float v = BrScalarToFloat(prgbrv[ibrv].map.v[1]);
            if (u < uMin)
                uMin = u;
            if (u > uMax)
                uMax = u;
            if (v < vMin)
                vMin = v;
            if (v > vMax)
                vMax = v;
        }

        if (std::fabs(uMax - uMin) <= 0.00001f && std::fabs(vMax - vMin) <= 0.00001f)
            continue;

        cDisagreements++;
        int32_t ibrvFirst = vertices[0];
        report << "Position group " << cDisagreements << ": xyz=("
               << BrScalarToFloat(prgbrv[ibrvFirst].p.v[0]) << ", "
               << BrScalarToFloat(prgbrv[ibrvFirst].p.v[1]) << ", "
               << BrScalarToFloat(prgbrv[ibrvFirst].p.v[2]) << ")\n";
        report << "  UV span: du=" << uMax - uMin << " dv=" << vMax - vMin << "\n";
        for (int32_t ibrv : vertices)
        {
            report << "  vertex " << ibrv << " uv=(" << BrScalarToFloat(prgbrv[ibrv].map.v[0]) << ", "
                   << BrScalarToFloat(prgbrv[ibrv].map.v[1]) << ") faces=";
            const std::vector<int32_t> &faces = rgFacesByVertex[(size_t)ibrv];
            for (size_t ifac = 0; ifac < faces.size(); ifac++)
            {
                if (ifac > 0)
                    report << ";";
                report << faces[ifac];
            }
            report << "\n";
        }
        report << "\n";
    }

    report << "Duplicate-position groups: " << cGroups << "\n";
    report << "Groups with differing UVs: " << cDisagreements << "\n";

    std::ofstream index(szIndex, std::ios::out | std::ios::app);
    if (index)
    {
        index.seekp(0, std::ios::end);
        if (index.tellp() == 0)
            index << "dump,ctg,cno,source,vertices,faces,duplicate_position_groups,uv_disagreements,obj,csv,report\n";
        index << iDump << ",0x" << std::hex << std::uppercase << (uint32_t)ctg << ",0x" << (uint32_t)cno
              << std::dec << "," << pszSource << "," << cbrv << "," << cbrf << "," << cGroups << ","
              << cDisagreements << "," << szBase << ".obj," << szBase << ".csv," << szBase
              << "_uv_report.txt\n";
    }
}

static float _UvFractionToFloat(br_ufraction value)
{
    return BrScalarToFloat(BrUFractionToScalar(value));
}

static void _UvWriteLe16(std::ofstream &out, uint16_t value)
{
    char rgb[2];
    rgb[0] = (char)(value & 0xff);
    rgb[1] = (char)((value >> 8) & 0xff);
    out.write(rgb, 2);
}

static void _UvWriteLe32(std::ofstream &out, uint32_t value)
{
    char rgb[4];
    rgb[0] = (char)(value & 0xff);
    rgb[1] = (char)((value >> 8) & 0xff);
    rgb[2] = (char)((value >> 16) & 0xff);
    rgb[3] = (char)((value >> 24) & 0xff);
    out.write(rgb, 4);
}

static const uint8_t *_PrgbUvTextureRow(const BPMP *pbpmp, int32_t yp)
{
    if (pbpmp == pvNil || pbpmp->pixels == pvNil || yp < 0 || yp >= pbpmp->height)
        return pvNil;

    const uint8_t *prgb = (const uint8_t *)pbpmp->pixels;
    int32_t cbRow = pbpmp->row_bytes;
    if (cbRow >= 0)
        return prgb + LwMul(pbpmp->base_y + yp, cbRow) + pbpmp->base_x;

    cbRow = -cbRow;
    return prgb + LwMul(pbpmp->height - 1 - (pbpmp->base_y + yp), cbRow) + pbpmp->base_x;
}

static bool _FWriteUvTexturePgm(const char *pszFile, const BPMP *pbpmp)
{
    if (pbpmp == pvNil || pbpmp->pixels == pvNil || pbpmp->type != BR_PMT_INDEX_8 || pbpmp->width == 0 ||
        pbpmp->height == 0)
        return fFalse;

    std::ofstream out(pszFile, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
        return fFalse;

    out << "P5\n" << pbpmp->width << " " << pbpmp->height << "\n255\n";
    for (int32_t yp = 0; yp < pbpmp->height; yp++)
    {
        const uint8_t *prgbRow = _PrgbUvTextureRow(pbpmp, yp);
        if (prgbRow == pvNil)
            return fFalse;
        out.write((const char *)prgbRow, pbpmp->width);
    }
    return out.good();
}

static bool _FWriteUvTextureBmp(const char *pszFile, const BPMP *pbpmp)
{
    if (pbpmp == pvNil || pbpmp->pixels == pvNil || pbpmp->type != BR_PMT_INDEX_8 || pbpmp->width == 0 ||
        pbpmp->height == 0)
        return fFalse;

    CLR rgclr[256];
    ClearPb(rgclr, SIZEOF(rgclr));
    PGL pglclr = GPT::PglclrGetPalette();
    if (pglclr != pvNil)
    {
        int32_t cclr = LwMin(pglclr->IvMac(), 256);
        for (int32_t iclr = 0; iclr < cclr; iclr++)
            pglclr->Get(iclr, &rgclr[iclr]);
        ReleasePpo(&pglclr);
    }
    else
    {
        for (int32_t iclr = 0; iclr < 256; iclr++)
        {
            rgclr[iclr].bRed = (uint8_t)iclr;
            rgclr[iclr].bGreen = (uint8_t)iclr;
            rgclr[iclr].bBlue = (uint8_t)iclr;
        }
    }

    uint32_t cbRowOut = ((uint32_t)pbpmp->width + 3U) & ~3U;
    uint32_t cbPixels = cbRowOut * (uint32_t)pbpmp->height;
    uint32_t fpPixels = 14U + 40U + 256U * 4U;
    uint32_t cbFile = fpPixels + cbPixels;

    std::ofstream out(pszFile, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
        return fFalse;

    out.put('B');
    out.put('M');
    _UvWriteLe32(out, cbFile);
    _UvWriteLe16(out, 0);
    _UvWriteLe16(out, 0);
    _UvWriteLe32(out, fpPixels);

    _UvWriteLe32(out, 40);
    _UvWriteLe32(out, pbpmp->width);
    _UvWriteLe32(out, pbpmp->height);
    _UvWriteLe16(out, 1);
    _UvWriteLe16(out, 8);
    _UvWriteLe32(out, 0);
    _UvWriteLe32(out, cbPixels);
    _UvWriteLe32(out, 0);
    _UvWriteLe32(out, 0);
    _UvWriteLe32(out, 256);
    _UvWriteLe32(out, 256);

    out.write((const char *)rgclr, SIZEOF(rgclr));

    char rgbPad[3] = {0, 0, 0};
    uint32_t cbPad = cbRowOut - pbpmp->width;
    for (int32_t yp = pbpmp->height - 1; yp >= 0; yp--)
    {
        const uint8_t *prgbRow = _PrgbUvTextureRow(pbpmp, yp);
        if (prgbRow == pvNil)
            return fFalse;
        out.write((const char *)prgbRow, pbpmp->width);
        if (cbPad > 0)
            out.write(rgbPad, cbPad);
    }
    return out.good();
}

static void _DumpUvMaterialFlagNames(std::ofstream &out, uint32_t grf)
{
    struct FLAGNAME
    {
        uint32_t grf;
        const char *psz;
    };
    static const FLAGNAME rgflag[] = {
        {BR_MATF_LIGHT, "LIGHT"},           {BR_MATF_PRELIT, "PRELIT"},
        {BR_MATF_SMOOTH, "SMOOTH"},         {BR_MATF_ENVIRONMENT_I, "ENVIRONMENT_I"},
        {BR_MATF_ENVIRONMENT_L, "ENVIRONMENT_L"}, {BR_MATF_PERSPECTIVE, "PERSPECTIVE"},
        {BR_MATF_DECAL, "DECAL"},           {BR_MATF_I_FROM_U, "I_FROM_U"},
        {BR_MATF_I_FROM_V, "I_FROM_V"},     {BR_MATF_U_FROM_I, "U_FROM_I"},
        {BR_MATF_V_FROM_I, "V_FROM_I"},     {BR_MATF_ALWAYS_VISIBLE, "ALWAYS_VISIBLE"},
        {BR_MATF_TWO_SIDED, "TWO_SIDED"},   {BR_MATF_FORCE_Z_0, "FORCE_Z_0"},
        {BR_MATF_DITHER, "DITHER"},
    };

    bool fFirst = fTrue;
    for (int32_t iflag = 0; iflag < SIZEOF(rgflag) / SIZEOF(rgflag[0]); iflag++)
    {
        if ((grf & rgflag[iflag].grf) == 0)
            continue;
        if (!fFirst)
            out << "|";
        out << rgflag[iflag].psz;
        fFirst = fFalse;
    }
    if (fFirst)
        out << "none";
}

static const char *_PszUvMaterialIdentifier(PBMTL pbmtl)
{
    if (pbmtl == pvNil || pbmtl->identifier == pvNil)
        return "";
    return pbmtl->identifier;
}

static int32_t _IgrpUvFace(PBMDL pbmdl, const BRF *pbrf, int32_t *pifacGroup, PBMTL *ppbmtlGroup)
{
    if (pifacGroup != pvNil)
        *pifacGroup = -1;
    if (ppbmtlGroup != pvNil)
        *ppbmtlGroup = pvNil;
    if (pbmdl == pvNil || pbrf == pvNil || pbmdl->face_groups == pvNil)
        return -1;

    uintptr_t upFace = (uintptr_t)pbrf;
    for (int32_t igrp = 0; igrp < pbmdl->nface_groups; igrp++)
    {
        br_face_group *pgrp = &pbmdl->face_groups[igrp];
        if (pgrp->faces == pvNil || pgrp->nfaces == 0)
            continue;
        uintptr_t upFirst = (uintptr_t)pgrp->faces;
        uintptr_t upLim = upFirst + (uintptr_t)pgrp->nfaces * SIZEOF(BRF);
        if (upFace < upFirst || upFace >= upLim)
            continue;

        if (pifacGroup != pvNil)
            *pifacGroup = (int32_t)((upFace - upFirst) / SIZEOF(BRF));
        if (ppbmtlGroup != pvNil)
            *ppbmtlGroup = pgrp->material;
        return igrp;
    }
    return -1;
}

/***************************************************************************
    Dump one BODY-part model/material association. This version records the
    actual prepared-face material pointer, the BRender face group that owns
    each face, and the effective material after actor fallback. This makes
    per-face material routing visible instead of assuming every face inherits
    the BODY actor material.
***************************************************************************/
void MODL::DumpBodyPart(PBMDL pbmdl, PBMTL pbmtl, const void *pvBody, int32_t ibact, int32_t ibset)
{
    if (!vfUvDump || pbmdl == pvNil || pbmtl == pvNil)
        return;

    UVASSOCKEY key = {pbmdl, pbmtl, pvBody, ibact, ibset};
    if (!vsetUvAssociations.insert(key).second)
        return;

    const BRV *prgbrv = pbmdl->prepared_vertices;
    const BRF *prgbrf = pbmdl->prepared_faces;
    int32_t cbrv = pbmdl->nprepared_vertices;
    int32_t cbrf = pbmdl->nprepared_faces;
    if (prgbrv == pvNil || prgbrf == pvNil || cbrv <= 0 || cbrf <= 0)
    {
        prgbrv = pbmdl->vertices;
        prgbrf = pbmdl->faces;
        cbrv = pbmdl->nvertices;
        cbrf = pbmdl->nfaces;
    }
    if (prgbrv == pvNil || prgbrf == pvNil || cbrv <= 0 || cbrf <= 0)
        return;

    char szDumpDir[kcchUvPath];
    if (!_FGetUvDumpDir(szDumpDir))
        return;

    UVMODELINFO info;
    ClearPb(&info, SIZEOF(info));
    std::map<const BMDL *, UVMODELINFO>::const_iterator it = vmapUvModels.find(pbmdl);
    if (it != vmapUvModels.end())
        info = it->second;
    else
    {
        info.ctg = kctgBmdl;
        info.cno = 0;
#if defined(KAUAI_WIN32)
        wsprintfA(info.szBase, "MODL_UNKNOWN_%p", pbmdl);
#else
        snprintf(info.szBase, SIZEOF(info.szBase), "MODL_UNKNOWN_%p", (const void *)pbmdl);
#endif
    }

    uint32_t iAssoc = ++vcUvAssociationDump;
    char szBase[kcchUvPath];
    char szObj[kcchUvPath];
    char szMtl[kcchUvPath];
    char szCsv[kcchUvPath];
    char szGroups[kcchUvPath];
    char szInfo[kcchUvPath];
    char szTextureBmp[kcchUvPath];
    char szTexturePgm[kcchUvPath];
    char szTextureRaw[kcchUvPath];
    char szAssocIndex[kcchUvPath];
#if defined(KAUAI_WIN32)
    wsprintfA(szBase, "%s_ASSOC_%06u_PART_%03d_SET_%03d", info.szBase, iAssoc, ibact, ibset);
    wsprintfA(szObj, "%s\\%s.obj", szDumpDir, szBase);
    wsprintfA(szMtl, "%s\\%s.mtl", szDumpDir, szBase);
    wsprintfA(szCsv, "%s\\%s_faces.csv", szDumpDir, szBase);
    wsprintfA(szGroups, "%s\\%s_face_groups.csv", szDumpDir, szBase);
    wsprintfA(szInfo, "%s\\%s_material.txt", szDumpDir, szBase);
    wsprintfA(szTextureBmp, "%s\\%s_texture.bmp", szDumpDir, szBase);
    wsprintfA(szTexturePgm, "%s\\%s_texture_indices.pgm", szDumpDir, szBase);
    wsprintfA(szTextureRaw, "%s\\%s_texture_raw.bin", szDumpDir, szBase);
    wsprintfA(szAssocIndex, "%s\\associations.csv", szDumpDir);
#else
    snprintf(szBase, SIZEOF(szBase), "%s_ASSOC_%06u_PART_%03d_SET_%03d", info.szBase, iAssoc, ibact, ibset);
    snprintf(szObj, SIZEOF(szObj), "%s/%s.obj", szDumpDir, szBase);
    snprintf(szMtl, SIZEOF(szMtl), "%s/%s.mtl", szDumpDir, szBase);
    snprintf(szCsv, SIZEOF(szCsv), "%s/%s_faces.csv", szDumpDir, szBase);
    snprintf(szGroups, SIZEOF(szGroups), "%s/%s_face_groups.csv", szDumpDir, szBase);
    snprintf(szInfo, SIZEOF(szInfo), "%s/%s_material.txt", szDumpDir, szBase);
    snprintf(szTextureBmp, SIZEOF(szTextureBmp), "%s/%s_texture.bmp", szDumpDir, szBase);
    snprintf(szTexturePgm, SIZEOF(szTexturePgm), "%s/%s_texture_indices.pgm", szDumpDir, szBase);
    snprintf(szTextureRaw, SIZEOF(szTextureRaw), "%s/%s_texture_raw.bin", szDumpDir, szBase);
    snprintf(szAssocIndex, SIZEOF(szAssocIndex), "%s/associations.csv", szDumpDir);
#endif

    std::ofstream obj(szObj, std::ios::out | std::ios::trunc);
    std::ofstream mtl(szMtl, std::ios::out | std::ios::trunc);
    std::ofstream csv(szCsv, std::ios::out | std::ios::trunc);
    std::ofstream groups(szGroups, std::ios::out | std::ios::trunc);
    std::ofstream material(szInfo, std::ios::out | std::ios::trunc);
    if (!obj || !mtl || !csv || !groups || !material)
        return;

    struct UVFACEINFO
    {
        int32_t igrp;
        int32_t ifacGroup;
        PBMTL pbmtlFace;
        PBMTL pbmtlGroup;
        PBMTL pbmtlEffective;
        const char *pszSource;
    };

    std::vector<UVFACEINFO> rgFaceInfo((size_t)cbrf);
    std::map<const BMTL *, int32_t> mapMaterialIds;
    mapMaterialIds[pbmtl] = 0;
    int32_t cFaceOverride = 0;
    int32_t cGroupOverride = 0;
    int32_t cActorFallback = 0;

    for (int32_t ibrf = 0; ibrf < cbrf; ibrf++)
    {
        UVFACEINFO &fi = rgFaceInfo[(size_t)ibrf];
        fi.pbmtlFace = prgbrf[ibrf].material;
        fi.igrp = _IgrpUvFace(pbmdl, &prgbrf[ibrf], &fi.ifacGroup, &fi.pbmtlGroup);
        if (fi.pbmtlFace != pvNil)
        {
            fi.pbmtlEffective = fi.pbmtlFace;
            fi.pszSource = "face";
            cFaceOverride++;
        }
        else if (fi.pbmtlGroup != pvNil)
        {
            fi.pbmtlEffective = fi.pbmtlGroup;
            fi.pszSource = "group";
            cGroupOverride++;
        }
        else
        {
            fi.pbmtlEffective = pbmtl;
            fi.pszSource = "actor";
            cActorFallback++;
        }
        if (fi.pbmtlEffective != pvNil && mapMaterialIds.find(fi.pbmtlEffective) == mapMaterialIds.end())
            mapMaterialIds[fi.pbmtlEffective] = (int32_t)mapMaterialIds.size();
    }

    obj << "# 3DMMEx -uvdump prepared-face material routing\n";
    obj << "# model=" << info.szBase << " body=" << pvBody << " part=" << ibact << " set=" << ibset << "\n";
    obj << "mtllib " << szBase << ".mtl\n";
    obj << std::setprecision(9);
    for (int32_t ibrv = 0; ibrv < cbrv; ibrv++)
    {
        obj << "v " << BrScalarToFloat(prgbrv[ibrv].p.v[0]) << " " << BrScalarToFloat(prgbrv[ibrv].p.v[1])
            << " " << BrScalarToFloat(prgbrv[ibrv].p.v[2]) << "\n";
    }
    for (int32_t ibrv = 0; ibrv < cbrv; ibrv++)
    {
        obj << "vt " << BrScalarToFloat(prgbrv[ibrv].map.v[0]) << " "
            << BrScalarToFloat(prgbrv[ibrv].map.v[1]) << "\n";
    }

    csv << "face,v0,v1,v2,smoothing,flags,face_material,face_identifier,group_index,group_face_index,"
           "group_material,group_identifier,actor_material,actor_identifier,effective_material,effective_identifier,"
           "effective_source,body,part,set,model_ctg,model_cno\n";

    int32_t imtlObjPrev = -1;
    for (int32_t ibrf = 0; ibrf < cbrf; ibrf++)
    {
        int32_t iv0 = prgbrf[ibrf].vertices[0];
        int32_t iv1 = prgbrf[ibrf].vertices[1];
        int32_t iv2 = prgbrf[ibrf].vertices[2];
        if (iv0 < 0 || iv0 >= cbrv || iv1 < 0 || iv1 >= cbrv || iv2 < 0 || iv2 >= cbrv)
            continue;

        const UVFACEINFO &fi = rgFaceInfo[(size_t)ibrf];
        int32_t imtlObj = fi.pbmtlEffective == pvNil ? -1 : mapMaterialIds[fi.pbmtlEffective];
        if (imtlObj != imtlObjPrev)
        {
            obj << "usemtl material_" << imtlObj << "\n";
            imtlObjPrev = imtlObj;
        }
        obj << "f " << iv0 + 1 << "/" << iv0 + 1 << " " << iv1 + 1 << "/" << iv1 + 1 << " " << iv2 + 1
            << "/" << iv2 + 1 << "\n";

        csv << ibrf << "," << iv0 << "," << iv1 << "," << iv2 << "," << prgbrf[ibrf].smoothing << ","
            << (uint32_t)prgbrf[ibrf].flags << "," << fi.pbmtlFace << ","
            << _PszUvMaterialIdentifier(fi.pbmtlFace) << "," << fi.igrp << "," << fi.ifacGroup << ","
            << fi.pbmtlGroup << "," << _PszUvMaterialIdentifier(fi.pbmtlGroup) << "," << pbmtl << ","
            << _PszUvMaterialIdentifier(pbmtl) << "," << fi.pbmtlEffective << ","
            << _PszUvMaterialIdentifier(fi.pbmtlEffective) << "," << fi.pszSource << "," << pvBody << "," << ibact
            << "," << ibset << ",0x" << std::hex << std::uppercase << (uint32_t)info.ctg << ",0x"
            << (uint32_t)info.cno << std::dec << "\n";
    }

    groups << "group,faces_ptr,nfaces,first_prepared_face,material,identifier\n";
    for (int32_t igrp = 0; igrp < pbmdl->nface_groups; igrp++)
    {
        br_face_group *pgrp = &pbmdl->face_groups[igrp];
        int32_t ifacFirst = -1;
        if (pgrp->faces != pvNil && prgbrf != pvNil)
        {
            uintptr_t upFirst = (uintptr_t)pgrp->faces;
            uintptr_t upPrepared = (uintptr_t)prgbrf;
            uintptr_t cbPrepared = (uintptr_t)cbrf * SIZEOF(BRF);
            if (upFirst >= upPrepared && upFirst < upPrepared + cbPrepared)
                ifacFirst = (int32_t)((upFirst - upPrepared) / SIZEOF(BRF));
        }
        groups << igrp << "," << pgrp->faces << "," << pgrp->nfaces << "," << ifacFirst << "," << pgrp->material
               << "," << _PszUvMaterialIdentifier(pgrp->material) << "\n";
    }

    for (std::map<const BMTL *, int32_t>::const_iterator imtl = mapMaterialIds.begin(); imtl != mapMaterialIds.end();
         ++imtl)
    {
        PBMTL pbmtlCur = (PBMTL)imtl->first;
        int32_t imtlCur = imtl->second;
        if (pbmtlCur == pvNil)
            continue;
        float r = BR_RED(pbmtlCur->colour) / 255.0f;
        float g = BR_GRN(pbmtlCur->colour) / 255.0f;
        float b = BR_BLU(pbmtlCur->colour) / 255.0f;
        mtl << std::setprecision(9);
        mtl << "newmtl material_" << imtlCur << "\n";
        mtl << "# pointer=" << pbmtlCur << " identifier=" << _PszUvMaterialIdentifier(pbmtlCur) << "\n";
        mtl << "Ka " << _UvFractionToFloat(pbmtlCur->ka) << " " << _UvFractionToFloat(pbmtlCur->ka) << " "
            << _UvFractionToFloat(pbmtlCur->ka) << "\n";
        mtl << "Kd " << r << " " << g << " " << b << "\n";
        mtl << "Ks " << _UvFractionToFloat(pbmtlCur->ks) << " " << _UvFractionToFloat(pbmtlCur->ks) << " "
            << _UvFractionToFloat(pbmtlCur->ks) << "\n";
        mtl << "Ns " << BrScalarToFloat(pbmtlCur->power) << "\n";
        mtl << "d " << (float)pbmtlCur->opacity / 255.0f << "\n";
        if (pbmtlCur->colour_map != pvNil && pbmtlCur->colour_map == pbmtl->colour_map)
            mtl << "map_Kd " << szBase << "_texture.bmp\n";
        mtl << "\n";
    }

    material << "3DMMEx prepared-face material routing diagnostic\n";
    material << "model_base=" << info.szBase << "\n";
    material << "model_ctg=0x" << std::hex << std::uppercase << (uint32_t)info.ctg << "\n";
    material << "model_cno=0x" << (uint32_t)info.cno << std::dec << "\n";
    material << "body=" << pvBody << "\npart=" << ibact << "\nset=" << ibset << "\n";
    material << "bmdl=" << pbmdl << "\nactor_bmtl=" << pbmtl << "\n";
    material << "actor_identifier=" << _PszUvMaterialIdentifier(pbmtl) << "\n";
    material << "prepared_faces=" << cbrf << "\nprepared_face_groups=" << pbmdl->nface_groups << "\n";
    material << "effective_from_face=" << cFaceOverride << "\n";
    material << "effective_from_group=" << cGroupOverride << "\n";
    material << "effective_from_actor=" << cActorFallback << "\n";
    material << "actor_colour_rgb=" << (uint32_t)BR_RED(pbmtl->colour) << "," << (uint32_t)BR_GRN(pbmtl->colour)
             << "," << (uint32_t)BR_BLU(pbmtl->colour) << "\n";
    material << "actor_flags=0x" << std::hex << std::uppercase << (uint32_t)pbmtl->flags << std::dec << "\n";
    material << "actor_flag_names=";
    _DumpUvMaterialFlagNames(material, pbmtl->flags);
    material << "\nactor_index_base=" << (uint32_t)pbmtl->index_base << "\n";
    material << "actor_index_range=" << (uint32_t)pbmtl->index_range << "\n";
    material << "actor_map_transform=[";
    for (int32_t ir = 0; ir < 3; ir++)
    {
        if (ir > 0)
            material << ";";
        material << BrScalarToFloat(pbmtl->map_transform.m[ir][0]) << ","
                 << BrScalarToFloat(pbmtl->map_transform.m[ir][1]);
    }
    material << "]\n\nface_groups:\n";
    for (int32_t igrp = 0; igrp < pbmdl->nface_groups; igrp++)
    {
        br_face_group *pgrp = &pbmdl->face_groups[igrp];
        material << "group[" << igrp << "] faces=" << pgrp->faces << " nfaces=" << pgrp->nfaces
                 << " material=" << pgrp->material << " identifier=" << _PszUvMaterialIdentifier(pgrp->material)
                 << "\n";
    }

    bool fTextureBmp = fFalse;
    bool fTexturePgm = fFalse;
    bool fTextureRaw = fFalse;
    if (pbmtl->colour_map != pvNil)
    {
        const BPMP *pbpmp = pbmtl->colour_map;
        material << "\nactor_texture_present=1\n";
        material << "actor_texture_width=" << pbpmp->width << "\nactor_texture_height=" << pbpmp->height << "\n";
        material << "actor_texture_row_bytes=" << pbpmp->row_bytes << "\nactor_texture_type=" << (uint32_t)pbpmp->type
                 << "\n";

        fTextureBmp = _FWriteUvTextureBmp(szTextureBmp, pbpmp);
        fTexturePgm = _FWriteUvTexturePgm(szTexturePgm, pbpmp);
        if (pbpmp->pixels != pvNil && pbpmp->height > 0 && pbpmp->row_bytes != 0)
        {
            std::ofstream raw(szTextureRaw, std::ios::out | std::ios::binary | std::ios::trunc);
            if (raw)
            {
                int32_t cbRow = pbpmp->row_bytes < 0 ? -pbpmp->row_bytes : pbpmp->row_bytes;
                int32_t cb = LwMul(cbRow, pbpmp->height);
                raw.write((const char *)pbpmp->pixels, cb);
                fTextureRaw = raw.good();
            }
        }
    }
    else
    {
        material << "\nactor_texture_present=0\n";
    }
    material << "texture_bmp_written=" << (fTextureBmp ? 1 : 0) << "\n";
    material << "texture_pgm_written=" << (fTexturePgm ? 1 : 0) << "\n";
    material << "texture_raw_written=" << (fTextureRaw ? 1 : 0) << "\n";

    std::ofstream associations(szAssocIndex, std::ios::out | std::ios::app);
    if (associations)
    {
        associations.seekp(0, std::ios::end);
        if (associations.tellp() == 0)
            associations << "association,model_base,ctg,cno,body,part,set,bmdl,actor_bmtl,face_groups,face_override,group_override,actor_fallback,texture,width,height,obj,mtl,faces,groups,material_info\n";
        associations << iAssoc << "," << info.szBase << ",0x" << std::hex << std::uppercase << (uint32_t)info.ctg
                     << ",0x" << (uint32_t)info.cno << std::dec << "," << pvBody << "," << ibact << "," << ibset
                     << "," << pbmdl << "," << pbmtl << "," << pbmdl->nface_groups << "," << cFaceOverride << ","
                     << cGroupOverride << "," << cActorFallback << "," << (pbmtl->colour_map != pvNil ? 1 : 0) << ","
                     << (pbmtl->colour_map != pvNil ? pbmtl->colour_map->width : 0) << ","
                     << (pbmtl->colour_map != pvNil ? pbmtl->colour_map->height : 0) << "," << szBase << ".obj,"
                     << szBase << ".mtl," << szBase << "_faces.csv," << szBase << "_face_groups.csv," << szBase
                     << "_material.txt\n";
    }
}

/** 3DMMv1.0: *************************************************************************
    Reads a BRender model from a .DAT file
***************************************************************************/
PMODL MODL::PmodlReadFromDat(FNI *pfni)
{
    AssertPo(pfni, ffniFile);

    STN stn;
    PMODL pmodl;

    pmodl = NewObj MODL;
    if (pvNil == pmodl)
        goto LFail;
    pfni->GetStnPath(&stn);
    SZS szs;
    stn.GetSzs(szs);
    pmodl->_pbmdl = BrModelLoad(szs);
    if (pvNil == pmodl->_pbmdl)
        goto LFail;
    pmodl->_pbmdl->flags |= BR_MODF_KEEP_ORIGINAL;
    BrModelPrepare(pmodl->_pbmdl, BR_MPREP_ALL);
    Assert(CchSz((PCSZ)(pmodl->_pbmdl->identifier)) >= SIZEOF(void *), "no room for pmodl ptr");
    CopyPb(&pmodl, pmodl->_pbmdl->identifier, SIZEOF(void *));
    AssertPo(pmodl, 0);
    return pmodl;
LFail:
    ReleasePpo(&pmodl);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Returns a pointer to the MODL that owns this BMDL
***************************************************************************/
PMODL MODL::PmodlFromBmdl(PBMDL pbmdl)
{
    AssertVarMem(pbmdl);
    PMODL pmodl = (PMODL) * (uintptr_t *)pbmdl->identifier;
    AssertPo(pmodl, 0);
    return pmodl;
}

/** 3DMMv1.0: *************************************************************************
    Destructor
***************************************************************************/
MODL::~MODL(void)
{
    AssertBaseThis(0);
    uint64_t qwPerfDestroy = MVIE::FPerformanceMode() ? MVIE::PerfNow() : 0;
    if (pvNil != _pbmdl)
    {
        vmapUvModels.erase(_pbmdl);
        BrModelRemove(_pbmdl);
        BrModelFree(_pbmdl);
    }
    if (MVIE::FPerformanceMode())
        MVIE::PerfRecordModelDestroy(MVIE::PerfElapsedUs(qwPerfDestroy));
}

#if defined(BRENDER_MODERN_14)
/***************************************************************************
    Convert modern runtime faces back to the exact 32-byte face records that
    3DMM stores in MODL chunks. BRender 1.4 no longer exposes its generated
    edge indices, so regenerate the legacy undirected edge numbering here.
***************************************************************************/
static bool _FWriteLegacyFaces(BRFF *prgbrff, const BRF *prgbrf, int32_t cbrf)
{
    std::map<uint32_t, uint16_t> mpEdge;
    uint32_t iedgeNext = 0;

    for (int32_t ibrf = 0; ibrf < cbrf; ++ibrf)
    {
        const BRF &brf = prgbrf[ibrf];
        BRFF &brff = prgbrff[ibrf];
        ClearPb(&brff, SIZEOF(brff));

        for (int32_t iv = 0; iv < 3; ++iv)
            brff.vertices[iv] = brf.vertices[iv];

        for (int32_t iedge = 0; iedge < 3; ++iedge)
        {
            uint16_t iv0 = brf.vertices[iedge];
            uint16_t iv1 = brf.vertices[(iedge + 1) % 3];
            uint16_t ivLo = iv0 < iv1 ? iv0 : iv1;
            uint16_t ivHi = iv0 < iv1 ? iv1 : iv0;
            uint32_t key = ((uint32_t)ivLo << 16) | (uint32_t)ivHi;
            std::map<uint32_t, uint16_t>::iterator it = mpEdge.find(key);
            if (it == mpEdge.end())
            {
                // Legacy br_model::nedges is 16-bit, so a pre-prepared MODL
                // cannot describe 65536 or more distinct edges.
                if (iedgeNext >= 0xFFFFu)
                    return fFalse;
                uint16_t iedgeLegacy = (uint16_t)iedgeNext++;
                mpEdge.insert(std::make_pair(key, iedgeLegacy));
                brff.edges[iedge] = iedgeLegacy;
            }
            else
            {
                brff.edges[iedge] = it->second;
            }
        }

        brff.material = 0;
        brff.smoothing = brf.smoothing;
        brff.flags = brf.flags;
        brff.n = brf.n;
        brff.d = brf.d;
    }

    return fTrue;
}
#endif

/***************************************************************************
    Writes a MODL to a chunk. fZeroPivot is used only by Actor Studio when a
    live TDF glyph has already gone through the Modern zero-pivot prepare
    bridge. In that case the public prepared vertices are the authoritative
    rendered model-space geometry, and serializing the old nonzero pivot would
    make a freshly loaded custom BMDL subtract that pivot a second time.
***************************************************************************/
bool MODL::_FWrite(PCFL pcfl, CTG ctg, CNO cno, bool fZeroPivot)
{
    AssertThis(0);
    AssertPo(pcfl, 0);

    int32_t cb;
    int32_t cbrgbrv;
    int32_t cbrgbrf;
    MODLF *pmodlf = pvNil;

    cbrgbrv = LwMul(_pbmdl->nprepared_vertices, SIZEOF(br_vertex));
#if defined(BRENDER_MODERN_14)
    cbrgbrf = LwMul(_pbmdl->nprepared_faces, SIZEOF(BRFF));
#else
    cbrgbrf = LwMul(_pbmdl->nprepared_faces, SIZEOF(br_face));
#endif
    cb = SIZEOF(MODLF) + cbrgbrv + cbrgbrf;
    if (!FAllocPv((void **)&pmodlf, cb, fmemClear, mprNormal))
        goto LFail;
    pmodlf->bo = kboCur;
    pmodlf->osk = koskCur;
    pmodlf->cver = _pbmdl->nprepared_vertices;
    pmodlf->cfac = _pbmdl->nprepared_faces;
    pmodlf->rRadius = _pbmdl->radius;
    pmodlf->brb = _pbmdl->bounds;
    if (fZeroPivot)
        BrVector3Set(&pmodlf->bvec3Pivot, rZero, rZero, rZero);
    else
        pmodlf->bvec3Pivot = _pbmdl->pivot;
    CopyPb(_pbmdl->prepared_vertices, PvAddBv(pmodlf, SIZEOF(MODLF)), cbrgbrv);
#if defined(BRENDER_MODERN_14)
    if (!_FWriteLegacyFaces((BRFF *)PvAddBv(pmodlf, SIZEOF(MODLF) + cbrgbrv),
                            _pbmdl->prepared_faces, _pbmdl->nprepared_faces))
        goto LFail;
#else
    CopyPb(_pbmdl->prepared_faces, PvAddBv(pmodlf, SIZEOF(MODLF) + cbrgbrv), cbrgbrf);
#endif
    if (!pcfl->FPutPv(pmodlf, cb, ctg, cno))
        goto LFail;
    FreePpv((void **)&pmodlf);
    return fTrue;
LFail:
    Warn("model save failed.");
    FreePpv((void **)&pmodlf);
    return fFalse;
}

bool MODL::FWrite(PCFL pcfl, CTG ctg, CNO cno)
{
    return _FWrite(pcfl, ctg, cno, fFalse);
}

bool MODL::FWriteActorStudioBake(PCFL pcfl, CTG ctg, CNO cno)
{
    const bool fZeroPivot = _fLegacyTdfPivotBridge;
#if defined(BRENDER_MODERN_14)
    if (fZeroPivot)
    {
        BrModernLog("MODL::FWriteActorStudioBake TDF live geometry zero-pivot model=%p pivot=(%.6g,%.6g,%.6g)",
                    _pbmdl,
                    (double)BrScalarToFloat(_pbmdl->pivot.v[0]),
                    (double)BrScalarToFloat(_pbmdl->pivot.v[1]),
                    (double)BrScalarToFloat(_pbmdl->pivot.v[2]));
    }
#endif
    return _FWrite(pcfl, ctg, cno, fZeroPivot);
}

/** 3DMMv1.0: *************************************************************************
    Adjust glyph for a TDF.  It is centered in X and Z, with Y at the
    baseline, and we do some voodoo to get "kerning" (really "variable
    interletter spacing") to work.
***************************************************************************/
void MODL::AdjustTdfCharacter(void)
{
    AssertThis(0);

    BRS dxrModl = Dxr();
    BRS dxrSpacing = _pbmdl->bounds.min.v[0];
    BRS dxr = BrsHalf(dxrModl) + dxrSpacing;
    BRS dzrModl = Dzr();
    BRS dzr = BrsHalf(dzrModl);
    int32_t cbrv = _pbmdl->nprepared_vertices;
    int32_t ibrv;

    for (ibrv = 0; ibrv < cbrv; ibrv++)
    {
        _pbmdl->prepared_vertices[ibrv].p.v[0] -= dxr;
        _pbmdl->prepared_vertices[ibrv].p.v[2] -= dzr;
    }
    _pbmdl->bounds.min.v[0] -= dxr;
    _pbmdl->bounds.max.v[0] -= dxr;
    _pbmdl->pivot.v[0] -= dxr;
    _pbmdl->bounds.min.v[0] -= dxrSpacing;
    if (dxrSpacing < BR_SCALAR(-0.01))
        dxrSpacing = BR_SCALAR(0.5);
    _pbmdl->bounds.max.v[0] += dxrSpacing;

    _pbmdl->bounds.min.v[2] -= dzr;
    _pbmdl->bounds.max.v[2] -= dzr;
    _pbmdl->pivot.v[2] -= dzr;
}

/** 3DMMv1.0: *************************************************************************
    Prelight a model
    REVIEW *****: make this code more general
***************************************************************************/
bool MODL::_FPrelight(int32_t cblit, BVEC3 *prgbvec3Light)
{
    AssertIn(cblit, 1, 10);
    AssertPvCb(prgbvec3Light, LwMul(cblit, SIZEOF(BVEC3)));
    AssertBaseThis(0);

    PBACT pbactWorld;
    PBACT pbactCamera;
    PBPMP pbpmpRGB;
    PBPMP pbpmpZ;
    PBACT pbactLight;
    BLIT blit;
    PBMTL pbmtl;
    int32_t iblit;
    BCAM bcam = {pvNil,
                 BR_CAMERA_PERSPECTIVE,
                 BR_ANGLE_DEG(60.0), // 3DMMv1.0: REVIEW *****
                 BR_SCALAR(1.0),
                 BR_SCALAR(100.0),
                 BR_SCALAR(16.0 / 9.0),
                 544,
                 306};

    const br_colour kbrcHilite = BR_COLOUR_RGB(255, 255, 255);
    const uint8_t kbOpaque = 0xff;
    const br_ufraction kbrufKaHilite = BR_UFRACTION(0.10);
    const br_ufraction kbrufKdHilite = BR_UFRACTION(0.60);
    const br_ufraction kbrufKsHilite = BR_UFRACTION(0.00);
    const BRS krPowerHilite = BR_SCALAR(50);
    const int32_t kiclrHilite = 108; // 3DMMv1.0: palette index for hilite color

    ClearPb(&blit, SIZEOF(BLIT));
    blit.colour = BR_COLOUR_RGB(0xff, 0xff, 0xff);
    blit.type = BR_LIGHT_DIRECT;
    blit.attenuation_c = BR_SCALAR(1.0);

    pbactWorld = BrActorAllocate(BR_ACTOR_NONE, pvNil);
    if (pvNil == pbactWorld)
        goto LFail;

    pbactCamera = BrActorAllocate(BR_ACTOR_CAMERA, &bcam);
    if (pvNil == pbactCamera)
        goto LFail;
    BrActorAdd(pbactWorld, pbactCamera);

    for (iblit = 0; iblit < cblit; iblit++)
    {
        pbactLight = BrActorAllocate(BR_ACTOR_LIGHT, pvNil);
        if (pvNil == pbactLight)
            goto LFail;
        pbactLight->type_data = &blit;
        BrActorAdd(pbactWorld, pbactLight);
        BrLightEnable(pbactLight);
    }

    pbpmpRGB = BrPixelmapAllocate(BR_PMT_INDEX_8, 544, 306, 0, 0);
    pbpmpZ = BrPixelmapAllocate(BR_PMT_DEPTH_16, 544, 306, 0, 0);

    pbmtl = BrMaterialAllocate(pvNil);
    if (pvNil == pbmtl)
        goto LFail;
    pbmtl->colour = kbrcHilite;
    pbmtl->ka = kbrufKaHilite;
    pbmtl->kd = kbrufKdHilite, pbmtl->ks = kbrufKsHilite;
    pbmtl->power = krPowerHilite;
    pbmtl->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD;
    pbmtl->index_base = kiclrHilite;
    pbmtl->index_range = 8;
    BrMaterialAdd(pbmtl);
    BrZbSceneRenderBegin(pbactWorld, pbactCamera, pbpmpRGB, pbpmpZ);
    BrSceneModelLight(_pbmdl, pbmtl, pbactWorld, pvNil);
    BrZbSceneRenderEnd();
    BrMaterialRemove(pbmtl);

    Assert(cblit == 1, "code needs to get smarter...");
    BrLightDisable(pbactLight);

    BrActorFree(pbactWorld);

    return fTrue;
LFail:
    BrActorFree(pbactWorld);
    return fFalse;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the MODL.
***************************************************************************/
void MODL::AssertValid(uint32_t grf)
{
    MODL_PAR::AssertValid(fobjAllocated);
    AssertVarMem(_pbmdl);
    Assert((PMODL) * (uintptr_t *)_pbmdl->identifier == this, "Bad MODL identifier");
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the MODL
***************************************************************************/
void MODL::MarkMem(void)
{
    AssertThis(0);

    MODL_PAR::MarkMem();
}
#endif // 3DMMv1.0: DEBUG
