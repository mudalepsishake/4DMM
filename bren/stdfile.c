/* 3DMMv1.0:
 * Copyright (c) 1993 Argonaut Software Ltd. All rights reserved.
 *
 * $Id: stdfile.c 1.5 1994/11/29 18:20:34 sam Exp $
 * $Locker: sam $
 *
 * Default file handler that uses <stdio.h>
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "brender.h"

/* BRender:
 * Access functions for stdio
 */
static br_uint_32 BR_CALLBACK BrStdioAttributes(void)
{
    return BR_FS_ATTR_READABLE | BR_FS_ATTR_WRITEABLE | BR_FS_ATTR_HAS_TEXT | BR_FS_ATTR_HAS_BINARY |
           BR_FS_ATTR_HAS_ADVANCE;
}

/* BRender:
 * Open a file for reading
 *
 * Use BRENDER_PATH to locate the file if necessary
 *
 * Having found the file, invoke a supplied callback, (if present)
 * to find out if the file is text, binary, or unknown
 *
 * Text mode files are primarily used for debugging but it can be
 * useful to allow hand editting of input data
 *
 * Return a void * file handle ('FILE *' cast to 'void *') or NULL
 * if open failed
 */
void *BR_CALLBACK BrStdioOpenRead(char *name, br_size_t n_magics, br_mode_test_cbfn *identify, int *mode_result)
{
    FILE *fh;
    char *br_path;
    char try_name[FILENAME_MAX + 1], *cp;
    br_uint_8 magics[BR_MAX_FILE_MAGICS];
    int open_mode = BR_FS_MODE_BINARY;

    /* BRender:
     * Try the current directory
     */
    strncpy(try_name, name, FILENAME_MAX);

    if ((fh = fopen(try_name, "rb")) == NULL)
    {

        /* BRender:
         * If that fails, and if a drive or a
         * directory were specified, don't search along path
         */
        if (strchr(name, ':') || strchr(name, '/') || strchr(name, '\\'))
            return NULL;

        /* BRender:
         * For each element of the path, if it exists
         */
        if ((br_path = getenv("BRENDER_PATH")) == NULL)
            return NULL;

        while (*br_path)
        {
            /* BRender:
             * Take characters until next seperator or terminator
             */
            cp = try_name;

            while (*br_path != ';' && *br_path != '\0')
                *cp++ = *br_path++;

            if (*br_path == ';')
                br_path++;

            /* BRender:
             * Add a directory seperator if none
             */
            if (cp != try_name && (*(cp - 1) != ':' && *(cp - 1) != '/' && *(cp - 1) != '\\'))
                *cp++ = '/';

            strcpy(cp, name);

            if (fh = fopen(try_name, "rb"))
                break;
        }

        if (fh == NULL)
            return NULL;
    }

    /* BRender:
     * Now have an open file, try and grab the first bytes from it
     */
    if (fread(magics, 1, n_magics, fh) != n_magics)
    {
        /* BRender:
         * Could not read all the required data, close and punt
         */
        fclose(fh);
        return NULL;
    }

    /* BRender:
     * Try and identify the file
     */
    if (identify)
        open_mode = identify(magics, n_magics);

    if (mode_result)
        *mode_result = open_mode;

    /* BRender:
     * Reopen file with it's new identity (or abandon if unknown identity)
     */
    switch (open_mode)
    {
    case BR_FS_MODE_TEXT:
        fh = freopen(try_name, "rt", fh);
        break;

    case BR_FS_MODE_BINARY:
        fh = freopen(try_name, "rb", fh);
        break;

    case BR_FS_MODE_UNKNOWN:
        fclose(fh);
        fh = NULL;
        break;

    default:
        BR_ERROR1("BrStdFileOpenRead: invalid open_mode %d", open_mode);
    }

    return fh;
}

/* BRender:
 * Open a file for writing, overwrites any existing file of the same name
 *
 * Return a void * file handle ('FILE *' cast to 'void *') or NULL
 * if open failed
 */
static void *BR_CALLBACK BrStdioOpenWrite(char *name, int mode)
{
    FILE *fh = NULL;

    fh = fopen(name, (mode == BR_FS_MODE_TEXT) ? "wt" : "wb");

    return fh;
}

/* BRender:
 * Close an open file
 */
static void BR_CALLBACK BrStdioClose(void *f)
{
    fclose((FILE *)f);
}

/* BRender:
 * Test EOF
 */
static int BR_CALLBACK BrStdioEof(void *f)
{
    return feof((FILE *)f);
}

/* BRender:
 * Read one character from file
 */
static int BR_CALLBACK BrStdioGetChar(void *f)
{
    return getc((FILE *)f);
}

/* BRender:
 * Write one character to file
 */
static void BR_CALLBACK BrStdioPutChar(int c, void *f)
{
    putc(c, (FILE *)f);
}

/* BRender:
 * Read a block from a file
 */
static br_size_t BR_CALLBACK BrStdioRead(void *buf, br_size_t size, unsigned int n, void *f)
{
    return fread(buf, size, n, (FILE *)f);
}

/* BRender:
 * Write a block to a file
 */
static br_size_t BR_CALLBACK BrStdioWrite(void *buf, br_size_t size, unsigned int n, void *f)
{
    return fwrite(buf, size, n, (FILE *)f);
}

/* BRender:
 * Read a line of text from stdin and trim any terminators
 *
 * Return length of line
 */
static br_size_t BR_CALLBACK BrStdioGetLine(char *buf, br_size_t buf_len, void *f)
{
    br_size_t l;

    if (fgets(buf, buf_len, (FILE *)f) == NULL)
        return 0;

    l = strlen(buf);

    if (l > 0 && buf[l - 1] == '\n')
        buf[--l] = '\0';

    return l;
}

/* BRender:
 * Write a line to text file, followed by newline
 */
void BR_CALLBACK BrStdioPutLine(char *buf, void *f)
{
    fputs(buf, (FILE *)f);
    putc('\n', (FILE *)f);
}

/* BRender:
 * Advance N bytes through a binary stream
 */
static void BR_CALLBACK BrStdioAdvance(br_size_t count, void *f)
{
    fseek((FILE *)f, (long int)count, SEEK_CUR);
}

/* BRender:
 * Filesystem structure
 */
br_filesystem BrStdioFilesystem = {
    "Standard IO", /* BRender: identifier */

    BrStdioAttributes, /* BRender: attributes */
    BrStdioOpenRead,   /* BRender: open_read  */
    BrStdioOpenWrite,  /* BRender: openwrite  */
    BrStdioClose,      /* BRender: close      */
    BrStdioEof,        /* BRender: eof		  */

    BrStdioGetChar, /* BRender: getchar	  */
    BrStdioPutChar, /* BRender: putchar	  */

    BrStdioRead,  /* BRender: read		  */
    BrStdioWrite, /* BRender: write	  */

    BrStdioGetLine, /* BRender: getline	  */
    BrStdioPutLine, /* BRender: putline	  */

    BrStdioAdvance, /* BRender: advance	  */
};

/* BRender:
 * Override global variable s.t. the default filesystem will be stdio
 */
br_filesystem *_BrDefaultFilesystem = &BrStdioFilesystem;
