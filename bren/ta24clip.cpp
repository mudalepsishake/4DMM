#include <stdint.h>

#if !defined(_MSC_VER)
#define __cdecl
#endif

extern void DiagLogBRender(const char *pszFormat, ...);

// The original 1995 RGB888 TA24 renderer is precompiled.  Its internal
// vertex layout begins with 16.16 fixed-point screen X and Y at offsets 0
// and 4.  It later extracts those high words as *unsigned* 16-bit values,
// so a clipped coordinate just below zero becomes 65535 and addresses row
// 65536 of the RGB/Z buffers.  Keep the legacy renderer intact; clip only
// triangles that cross the 544x306 viewport before handing them to it.
//
// The 3DMM-era TA24 vertex payload is at least 44 bytes.  Disassembly of
// TriangleRenderPIZ2TA24_orig shows the components it consumes at:
//   +0x00 SX, +0x04 SY, +0x08 SZ, +0x24 U, +0x28 V.
// clr29 incorrectly modeled the payload as five contiguous dwords, so newly
// generated clip vertices ended at +0x10 while the original rasterizer read
// U/V from +0x24/+0x28.  The resulting out-of-bounds U/V values produced the
// position-dependent static/smeared textures on triangles crossing the
// viewport.  Preserve the full observed 44-byte payload and only interpolate
// the five components whose semantics are confirmed.
struct TA24Vertex
{
    int32_t comp[11];
};

extern "C" void __cdecl TriangleRenderPIZ2TA24_orig(TA24Vertex *a,
                                                     TA24Vertex *b,
                                                     TA24Vertex *c);

enum
{
    kTA24ScreenX = 0,
    kTA24ScreenY = 1,
    kTA24ScreenZ = 2,
    kTA24TextureU = 9,
    kTA24TextureV = 10,
    kTA24ComponentCount = 11,
    kTA24MaxVertices = 8,
    kTA24ViewportWidth = 544,
    kTA24ViewportHeight = 306
};

static const int32_t kTA24MaxX = (kTA24ViewportWidth << 16) - 1;
static const int32_t kTA24MaxY = (kTA24ViewportHeight << 16) - 1;

static long s_cGuardCalls = 0;
static long s_cClippedTriangles = 0;

static void CopyVertex(TA24Vertex *dst, const TA24Vertex *src)
{
    for (int i = 0; i < kTA24ComponentCount; ++i)
        dst->comp[i] = src->comp[i];
}

static bool IsInside(const TA24Vertex *v, int axis, int32_t bound, bool keepGreater)
{
    return keepGreater ? v->comp[axis] >= bound : v->comp[axis] <= bound;
}

static void IntersectVertex(TA24Vertex *out,
                            const TA24Vertex *a,
                            const TA24Vertex *b,
                            int axis,
                            int32_t bound)
{
    const int64_t av = a->comp[axis];
    const int64_t bv = b->comp[axis];
    const int64_t den = bv - av;

    if (den == 0)
    {
        CopyVertex(out, a);
        out->comp[axis] = bound;
        return;
    }

    const int64_t num = (int64_t)bound - av;

    // Preserve unknown/unused payload fields exactly from A.  TA24 itself
    // only consumes SX/SY/SZ/U/V, so interpolate only those confirmed fields.
    // This avoids inventing semantics for the six intervening dwords while
    // still providing valid texture coordinates to the legacy rasterizer.
    CopyVertex(out, a);

    const int components[] = {
        kTA24ScreenX,
        kTA24ScreenY,
        kTA24ScreenZ,
        kTA24TextureU,
        kTA24TextureV
    };

    for (unsigned i = 0; i < sizeof(components) / sizeof(components[0]); ++i)
    {
        const int component = components[i];
        const int64_t va = a->comp[component];
        const int64_t vb = b->comp[component];
        out->comp[component] = (int32_t)(va + ((vb - va) * num) / den);
    }

    out->comp[axis] = bound;
}

static int ClipPlane(const TA24Vertex *in,
                     int count,
                     TA24Vertex *out,
                     int axis,
                     int32_t bound,
                     bool keepGreater)
{
    if (count <= 0)
        return 0;

    int outCount = 0;
    const TA24Vertex *prev = &in[count - 1];
    bool prevInside = IsInside(prev, axis, bound, keepGreater);

    for (int i = 0; i < count; ++i)
    {
        const TA24Vertex *cur = &in[i];
        const bool curInside = IsInside(cur, axis, bound, keepGreater);

        if (curInside != prevInside)
        {
            if (outCount >= kTA24MaxVertices)
                return 0;
            IntersectVertex(&out[outCount++], prev, cur, axis, bound);
        }

        if (curInside)
        {
            if (outCount >= kTA24MaxVertices)
                return 0;
            CopyVertex(&out[outCount++], cur);
        }

        prev = cur;
        prevInside = curInside;
    }

    return outCount;
}

static bool TriangleInside(const TA24Vertex *a, const TA24Vertex *b, const TA24Vertex *c)
{
    return a->comp[kTA24ScreenX] >= 0 && a->comp[kTA24ScreenX] <= kTA24MaxX &&
           a->comp[kTA24ScreenY] >= 0 && a->comp[kTA24ScreenY] <= kTA24MaxY &&
           b->comp[kTA24ScreenX] >= 0 && b->comp[kTA24ScreenX] <= kTA24MaxX &&
           b->comp[kTA24ScreenY] >= 0 && b->comp[kTA24ScreenY] <= kTA24MaxY &&
           c->comp[kTA24ScreenX] >= 0 && c->comp[kTA24ScreenX] <= kTA24MaxX &&
           c->comp[kTA24ScreenY] >= 0 && c->comp[kTA24ScreenY] <= kTA24MaxY;
}

extern "C" void __cdecl TriangleRenderPIZ2TA24(TA24Vertex *a,
                                                TA24Vertex *b,
                                                TA24Vertex *c)
{
    ++s_cGuardCalls;
    if (s_cGuardCalls == 1)
        DiagLogBRender("clr31 TA24 source guard ACTIVE payload=44 fields=SX@00,SY@04,SZ@08,U@24,V@28 viewport=544x306");

    if (TriangleInside(a, b, c))
    {
        TriangleRenderPIZ2TA24_orig(a, b, c);
        return;
    }

    ++s_cClippedTriangles;
    if (s_cClippedTriangles <= 32)
    {
        DiagLogBRender("clr31 TA24 clip #%ld A=(%ld,%ld) B=(%ld,%ld) C=(%ld,%ld)",
                       s_cClippedTriangles,
                       (long)a->comp[kTA24ScreenX], (long)a->comp[kTA24ScreenY],
                       (long)b->comp[kTA24ScreenX], (long)b->comp[kTA24ScreenY],
                       (long)c->comp[kTA24ScreenX], (long)c->comp[kTA24ScreenY]);
    }

    TA24Vertex p0[kTA24MaxVertices];
    TA24Vertex p1[kTA24MaxVertices];
    TA24Vertex *src = p0;
    TA24Vertex *dst = p1;

    CopyVertex(&p0[0], a);
    CopyVertex(&p0[1], b);
    CopyVertex(&p0[2], c);
    int count = 3;

#define CLIP_TA24(axis, bound, keepGreater)                                      \
    do                                                                            \
    {                                                                             \
        count = ClipPlane(src, count, dst, axis, bound, keepGreater);             \
        if (count < 3)                                                             \
            return;                                                                \
        TA24Vertex *tmp = src;                                                     \
        src = dst;                                                                 \
        dst = tmp;                                                                 \
    } while (0)

    CLIP_TA24(kTA24ScreenX, 0, true);
    CLIP_TA24(kTA24ScreenX, kTA24MaxX, false);
    CLIP_TA24(kTA24ScreenY, 0, true);
    CLIP_TA24(kTA24ScreenY, kTA24MaxY, false);

#undef CLIP_TA24

    for (int i = 1; i + 1 < count; ++i)
        TriangleRenderPIZ2TA24_orig(&src[0], &src[i], &src[i + 1]);
}
