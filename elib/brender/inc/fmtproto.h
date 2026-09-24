/* BRender:
 * Copyright (c) 1993-1995 by Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: fmtproto.h 1.8 1995/05/30 16:57:38 sam Exp $
 * $Locker:  $
 *
 * Function prototypes for foreign file format support
 */
#ifndef _FMTPROTO_H_
#define _FMTPROTO_H_

#ifdef __cplusplus
extern "C"
{
#endif

    /* BRender:
     * Eric Haines' NFF format
     */
    br_model *BR_PUBLIC_ENTRY BrFmtNFFLoad(char *name);

    /* BRender:
     * 3D Studio .ASC files
     */
    br_uint_32 BR_PUBLIC_ENTRY BrFmtASCLoad(char *name, br_model **mtable, br_uint_16 max_models);

    /* BRender:
     * Script files
     */
    br_uint_32 BR_PUBLIC_ENTRY BrFmtScriptMaterialLoadMany(char *filename, br_material **materials, br_uint_16 num);
    br_material *BR_PUBLIC_ENTRY BrFmtScriptMaterialLoad(char *filename);

    br_uint_32 BR_PUBLIC_ENTRY BrFmtScriptMaterialSaveMany(char *filename, br_material **materials, br_uint_16 num);
    br_uint_32 BR_PUBLIC_ENTRY BrFmtScriptMaterialSave(char *filename, br_material *ptr);

    /* BRender:
     * Windows .BMP files
     */
    br_pixelmap *BR_PUBLIC_ENTRY BrFmtBMPLoad(char *name, br_uint_32 flags);

    /* BRender:
     * .TGA files
     */
    br_pixelmap *BR_PUBLIC_ENTRY BrFmtTGALoad(char *name, br_uint_32 flags);

    /* BRender:
     * .GIF files
     */
    br_pixelmap *BR_PUBLIC_ENTRY BrFmtGIFLoad(char *name, br_uint_32 flags);

    /* BRender:
     * .IFF/.LBM files
     */
    br_pixelmap *BR_PUBLIC_ENTRY BrFmtIFFLoad(char *name, br_uint_32 flags);

#ifdef __cplusplus
};
#endif
#endif
