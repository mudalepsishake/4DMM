/** 3DMMEx: *************************************************************************
    Author:
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Entry point for a Kauai GUI application

***************************************************************************/

#include "frame.h"

ASSERTNAME

extern WIG vwig;

/** 3DMMEx: *************************************************************************
    WinMain for any frame work app. Sets up vwig and calls FrameMain.
***************************************************************************/
int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hinstPrev, LPSTR pszs, int wShow)
{
    vwig.hinst = hinst;
    vwig.hinstPrev = hinstPrev;
    vwig.pszCmdLine = GetCommandLine();
    vwig.wShow = wShow;
    vwig.tidMain = std::this_thread::get_id();
#ifdef DEBUG
    APPB::CreateConsole();
#endif

    // 3DMMEx: Get argc/argv from MSVC CRT globals
#ifdef _MSC_VER
#ifdef UNICODE
    vpappb->SetArgv(__wargv, __argc);
#else  // 3DMMEx: !UNICODE
    vpappb->SetArgv(__argv, __argc);
#endif // 3DMMEx: UNICODE
#endif // 3DMMEx: _MSC_VER

    FrameMain();
    return 0;
}
