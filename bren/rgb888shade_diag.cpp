#include <stdint.h>

#if !defined(_MSC_VER)
#define __cdecl
#endif

extern void DiagLogBRender(const char *pszFormat, ...);

// Diagnostic wrapper for the original BRender RGB888 Gouraud solid triangle
// rasterizer.  Disassembly of tt24_piz.obj confirms the renderer consumes
// screen X/Y/Z at +0x00/+0x04/+0x08 and final per-vertex RGB lighting values
// at +0x30/+0x34/+0x38.  actorlight34 also uses this already-established
// wrapper as the safe viewport clipping boundary for oversized solid triangles.
//
// The library copy included with tdtdiag1 renames only the renderer's defining
// symbol to TriangleRenderPIZ2I_RGB_88O.  zbsetup.obj still requests the normal
// TriangleRenderPIZ2I_RGB_888 symbol, so that call lands here first.
struct RGB888SolidVertex
{
    int32_t comp[15];
};

extern "C" void __cdecl TriangleRenderPIZ2I_RGB_88O(RGB888SolidVertex *a,
                                                     RGB888SolidVertex *b,
                                                     RGB888SolidVertex *c);

enum
{
    kCompSX = 0,
    kCompSY = 1,
    kCompSZ = 2,
    kCompR = 12,
    kCompG = 13,
    kCompB = 14,
    kCompCount = 15,
    kClipMaxVertices = 8,
    kViewportWidth = 544,
    kViewportHeight = 306
};

static const int32_t kViewportMaxX = (kViewportWidth << 16) - 1;
static const int32_t kViewportMaxY = (kViewportHeight << 16) - 1;

static long s_cCalls = 0;
static long s_cClippedTriangles = 0;

static void CopySolidVertex(RGB888SolidVertex *dst, const RGB888SolidVertex *src)
{
    for (int i = 0; i < kCompCount; ++i)
        dst->comp[i] = src->comp[i];
}

static bool SolidVertexInside(const RGB888SolidVertex *v, int axis, int32_t bound, bool keepGreater)
{
    return keepGreater ? v->comp[axis] >= bound : v->comp[axis] <= bound;
}

static void IntersectSolidVertex(RGB888SolidVertex *out,
                                 const RGB888SolidVertex *a,
                                 const RGB888SolidVertex *b,
                                 int axis,
                                 int32_t bound)
{
    const double av = (double)a->comp[axis];
    const double bv = (double)b->comp[axis];
    const double den = bv - av;

    CopySolidVertex(out, a);
    if (den == 0.0)
    {
        out->comp[axis] = bound;
        return;
    }

    const double t = ((double)bound - av) / den;
    const int components[] = {kCompSX, kCompSY, kCompSZ, kCompR, kCompG, kCompB};
    for (unsigned i = 0; i < sizeof(components) / sizeof(components[0]); ++i)
    {
        const int component = components[i];
        const double va = (double)a->comp[component];
        const double vb = (double)b->comp[component];
        out->comp[component] = (int32_t)(va + (vb - va) * t);
    }
    out->comp[axis] = bound;
}

static int ClipSolidPlane(const RGB888SolidVertex *in,
                          int count,
                          RGB888SolidVertex *out,
                          int axis,
                          int32_t bound,
                          bool keepGreater)
{
    if (count <= 0)
        return 0;

    int outCount = 0;
    const RGB888SolidVertex *prev = &in[count - 1];
    bool prevInside = SolidVertexInside(prev, axis, bound, keepGreater);

    for (int i = 0; i < count; ++i)
    {
        const RGB888SolidVertex *cur = &in[i];
        const bool curInside = SolidVertexInside(cur, axis, bound, keepGreater);

        if (curInside != prevInside)
        {
            if (outCount >= kClipMaxVertices)
                return 0;
            IntersectSolidVertex(&out[outCount++], prev, cur, axis, bound);
        }
        if (curInside)
        {
            if (outCount >= kClipMaxVertices)
                return 0;
            CopySolidVertex(&out[outCount++], cur);
        }

        prev = cur;
        prevInside = curInside;
    }
    return outCount;
}

static bool SolidTriangleInside(const RGB888SolidVertex *a,
                                const RGB888SolidVertex *b,
                                const RGB888SolidVertex *c)
{
    return a->comp[kCompSX] >= 0 && a->comp[kCompSX] <= kViewportMaxX &&
           a->comp[kCompSY] >= 0 && a->comp[kCompSY] <= kViewportMaxY &&
           b->comp[kCompSX] >= 0 && b->comp[kCompSX] <= kViewportMaxX &&
           b->comp[kCompSY] >= 0 && b->comp[kCompSY] <= kViewportMaxY &&
           c->comp[kCompSX] >= 0 && c->comp[kCompSX] <= kViewportMaxX &&
           c->comp[kCompSY] >= 0 && c->comp[kCompSY] <= kViewportMaxY;
}

static int ComponentByte(const RGB888SolidVertex *v, int i)
{
    return (int)((uint32_t)v->comp[i] >> 16) & 0xff;
}

extern "C" void __cdecl TriangleRenderPIZ2I_RGB_888(RGB888SolidVertex *a,
                                                     RGB888SolidVertex *b,
                                                     RGB888SolidVertex *c)
{
    ++s_cCalls;

    if (s_cCalls == 1)
    {
        DiagLogBRender("tdtdiag1 RGB888 Gouraud solid probe ACTIVE fields=SX@00,SY@04,SZ@08,R@30,G@34,B@38");
    }

    // A clean test scene containing only the flattened 3D Word makes the
    // first few dozen solid triangles overwhelmingly useful.  Cap output so
    // -logs does not turn a single frame into a geological epoch.
    if (s_cCalls <= 160)
    {
        const int ar = ComponentByte(a, kCompR);
        const int ag = ComponentByte(a, kCompG);
        const int ab = ComponentByte(a, kCompB);
        const int br = ComponentByte(b, kCompR);
        const int bg = ComponentByte(b, kCompG);
        const int bb = ComponentByte(b, kCompB);
        const int cr = ComponentByte(c, kCompR);
        const int cg = ComponentByte(c, kCompG);
        const int cb = ComponentByte(c, kCompB);

        DiagLogBRender(
            "TDT RGB888 tri #%ld Axy=(%ld,%ld) rgb=(%d,%d,%d) Bxy=(%ld,%ld) rgb=(%d,%d,%d) Cxy=(%ld,%ld) rgb=(%d,%d,%d)",
            s_cCalls,
            (long)a->comp[kCompSX], (long)a->comp[kCompSY], ar, ag, ab,
            (long)b->comp[kCompSX], (long)b->comp[kCompSY], br, bg, bb,
            (long)c->comp[kCompSX], (long)c->comp[kCompSY], cr, cg, cb);
    }

    // actorlight34: the Source-BRender RGB888 solid rasterizer assumes the
    // triangle has already been clipped. Giant/near-camera models can violate
    // that assumption and produce negative/oversized framebuffer indices.
    // Keep the fast path untouched for ordinary triangles and clip only the
    // exceptional ones before entering the legacy pixel loop.
    if (SolidTriangleInside(a, b, c))
    {
        TriangleRenderPIZ2I_RGB_88O(a, b, c);
        return;
    }

    ++s_cClippedTriangles;
    if (s_cClippedTriangles <= 32)
    {
        DiagLogBRender("actorlight34 RGB888 solid clip #%ld A=(%ld,%ld) B=(%ld,%ld) C=(%ld,%ld)",
                       s_cClippedTriangles,
                       (long)a->comp[kCompSX], (long)a->comp[kCompSY],
                       (long)b->comp[kCompSX], (long)b->comp[kCompSY],
                       (long)c->comp[kCompSX], (long)c->comp[kCompSY]);
    }

    RGB888SolidVertex p0[kClipMaxVertices];
    RGB888SolidVertex p1[kClipMaxVertices];
    RGB888SolidVertex *src = p0;
    RGB888SolidVertex *dst = p1;

    CopySolidVertex(&p0[0], a);
    CopySolidVertex(&p0[1], b);
    CopySolidVertex(&p0[2], c);
    int count = 3;

#define CLIP_SOLID(axis, bound, keepGreater)                                    \
    do                                                                           \
    {                                                                            \
        count = ClipSolidPlane(src, count, dst, axis, bound, keepGreater);       \
        if (count < 3)                                                            \
            return;                                                               \
        RGB888SolidVertex *tmp = src;                                             \
        src = dst;                                                                \
        dst = tmp;                                                                \
    } while (0)

    CLIP_SOLID(kCompSX, 0, true);
    CLIP_SOLID(kCompSX, kViewportMaxX, false);
    CLIP_SOLID(kCompSY, 0, true);
    CLIP_SOLID(kCompSY, kViewportMaxY, false);

#undef CLIP_SOLID

    for (int i = 1; i + 1 < count; ++i)
        TriangleRenderPIZ2I_RGB_88O(&src[0], &src[i], &src[i + 1]);
}
