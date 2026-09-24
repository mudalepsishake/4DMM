/*
 * BRender 1.4 / 3DMM fixed-point compatibility glue.
 *
 * Modern BRender still exposes BrFixedMulDiv in the public fixed-point API,
 * and the 3DMM fixed ABI uses it through BR_MULDIV, but BRender 1.4's
 * core/math/fixed.c no longer supplies the implementation.
 *
 * For 16.16 fixed values, the encoded result of (a * b) / c is simply the
 * signed 64-bit product divided by c.  This matches the original BRender x86
 * implementation (IMUL followed by IDIV) while avoiding 32-bit overflow in
 * the intermediate product.
 */
#include "brender.h"

#if defined(BRENDER_MODERN_14)
extern "C" br_fixed_ls BR_ASM_CALL BrFixedMulDiv(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c)
{
    if (c == 0)
        return 0;

    return (br_fixed_ls)(((br_int_64)a * (br_int_64)b) / c);
}
#endif
