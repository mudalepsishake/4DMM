#ifndef GPU_FLOAT_H_
#define GPU_FLOAT_H_

#include <math.h>

/*
 * OpenGL uniform/vertex buffers are always 32-bit floating point, even when
 * BRender itself is built with the legacy 16.16 fixed-point scalar ABI.
 * Keep the conversion at the renderer boundary instead of leaking the GPU
 * representation back into BRender's public/runtime structures.
 */
static inline br_float BrGLScalarToFloat(br_scalar v)
{
    return (br_float)BrScalarToFloat(v);
}

static inline void BrGLVector2ToFloat(br_vector2_f *dst, const br_vector2 *src)
{
    dst->v[0] = BrGLScalarToFloat(src->v[0]);
    dst->v[1] = BrGLScalarToFloat(src->v[1]);
}

static inline void BrGLVector3ToFloat(br_vector3_f *dst, const br_vector3 *src)
{
    dst->v[0] = BrGLScalarToFloat(src->v[0]);
    dst->v[1] = BrGLScalarToFloat(src->v[1]);
    dst->v[2] = BrGLScalarToFloat(src->v[2]);
}

static inline void BrGLVector4ToFloat(br_vector4_f *dst, const br_vector4 *src)
{
    dst->v[0] = BrGLScalarToFloat(src->v[0]);
    dst->v[1] = BrGLScalarToFloat(src->v[1]);
    dst->v[2] = BrGLScalarToFloat(src->v[2]);
    dst->v[3] = BrGLScalarToFloat(src->v[3]);
}

static inline void BrGLMatrix4ToFloat(br_matrix4_f *dst, const br_matrix4 *src)
{
    for(int r = 0; r < 4; ++r)
        for(int c = 0; c < 4; ++c)
            dst->m[r][c] = BrGLScalarToFloat(src->m[r][c]);
}

static inline void BrGLMatrix23ToMatrix4Float(br_matrix4_f *dst, const br_matrix23 *src)
{
    br_matrix4 tmp;
    BrMatrix4Copy23(&tmp, src);
    BrGLMatrix4ToFloat(dst, &tmp);
}

static inline void BrGLMatrix4Orthographic(br_matrix4_f *dst, br_float left, br_float right, br_float bottom, br_float top, br_float znear,
                                           br_float zfar)
{
    br_matrix4 tmp;
    BrMatrix4Orthographic(&tmp, BrFloatToScalar(left), BrFloatToScalar(right), BrFloatToScalar(bottom), BrFloatToScalar(top),
                          BrFloatToScalar(znear), BrFloatToScalar(zfar));
    BrGLMatrix4ToFloat(dst, &tmp);
}

static inline void BrGLVector4FSet(br_vector4_f *dst, br_float x, br_float y, br_float z, br_float w)
{
    dst->v[0] = x;
    dst->v[1] = y;
    dst->v[2] = z;
    dst->v[3] = w;
}

static inline void BrGLVector4FColourSet(br_vector4_f *dst, br_colour colour)
{
    dst->v[0] = BR_RED(colour) / 255.0f;
    dst->v[1] = BR_GRN(colour) / 255.0f;
    dst->v[2] = BR_BLU(colour) / 255.0f;
    dst->v[3] = BR_ALPHA(colour) / 255.0f;
}

static inline void BrGLVector4FAccumulateScale(br_vector4_f *dst, const br_vector4_f *src, br_float scale)
{
    for(int i = 0; i < 4; ++i)
        dst->v[i] += src->v[i] * scale;
}

static inline void BrGLVector4FScale(br_vector4_f *dst, const br_vector4_f *src, br_float scale)
{
    for(int i = 0; i < 4; ++i)
        dst->v[i] = src->v[i] * scale;
}

static inline void BrGLVector4FNormalise(br_vector4_f *dst, const br_vector4_f *src)
{
    const br_float len = sqrtf(src->v[0] * src->v[0] + src->v[1] * src->v[1] + src->v[2] * src->v[2] + src->v[3] * src->v[3]);
    if(len <= 0.0f) {
        BrGLVector4FSet(dst, 0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const br_float inv = 1.0f / len;
    BrGLVector4FScale(dst, src, inv);
}

static inline void BrGLVector4FClamp(br_vector4_f *dst, const br_vector4_f *src, br_float minv, br_float maxv)
{
    for(int i = 0; i < 4; ++i) {
        br_float v = src->v[i];
        if(v < minv)
            v = minv;
        else if(v > maxv)
            v = maxv;
        dst->v[i] = v;
    }
}

#endif /* GPU_FLOAT_H_ */
