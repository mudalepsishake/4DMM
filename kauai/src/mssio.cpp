/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    A message sink (MSNK) wrapper around a stdio file.

***************************************************************************/
#include <stdio.h>
#include "util.h"
#include "mssio.h"
ASSERTNAME

/** 3DMMv1.0: *************************************************************************
    Constructor for a standard I/O message sink.
***************************************************************************/
MSSIO::MSSIO(FILE *pfile)
{
    _pfile = pfile;
    _fError = fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Prints a message to stderr.
***************************************************************************/
void MSSIO::ReportLine(const PCSZ psz)
{
    AssertThis(0);
    AssertSz(psz);

#ifdef UNICODE
    _fError |= 0 > fwprintf(_pfile, PszLit("%s\n"), psz);
#else
    _fError |= 0 > fprintf(_pfile, "%s\n", psz);
#endif
}

/** 3DMMv1.0: *************************************************************************
    Dump a line to stdout.
***************************************************************************/
void MSSIO::Report(const PCSZ psz)
{
    AssertThis(0);
    AssertSz(psz);

#ifdef UNICODE
    _fError |= 0 > fwprintf(_pfile, PszLit("%s"), psz);
#else
    _fError |= 0 > fprintf(_pfile, "%s", psz);
#endif
}

/** 3DMMv1.0: *************************************************************************
    Return whether there has been an error writing to this message sink.
***************************************************************************/
bool MSSIO::FError(void)
{
    AssertThis(0);
    return _fError;
}
