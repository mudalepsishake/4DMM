/* BRender:
 * Copyright (c) 1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: loadpix.h 1.1 1997/12/10 16:58:54 jon Exp $
 * $Locker: $
 *
 */
#ifndef _LOADPIX_H_
#define _LOADPIX_H_

/* BRender:
 * Pixelmap save types
 */
enum {
    T_PALETTE,  /* BRender: save palette information	*/
    T_IMAGE,    /* BRender: save bitmap information	*/
    T_PIXELMAP, /* BRender: save pixelmaps		*/
    T_TARGA,    /* BRender: save uncompressed targa	*/
};

typedef br_pixelmap *BR_PUBLIC_ENTRY load_fn(const char *name, br_uint_32 type);

typedef struct Input_Types {
    char    *name;
    load_fn *function;
} Input_Types;

br_pixelmap *BR_PUBLIC_ENTRY T_LoadPixelmap(const char *filename, br_uint_32 type);
br_pixelmap                 *T_LoadRawPixelmap(char *filename, t_pixelmap_cbfn_info *cbfn_command);

extern Input_Types InputFileTypes[];
extern int         InputFileTypesSize;

extern int InputType;

#endif
