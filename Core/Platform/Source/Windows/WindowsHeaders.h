#pragma once

#ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0A00   // Windows 10 - GetDpiForWindow, WM_DPICHANGED
#endif
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#ifndef UNICODE
    #define UNICODE
#endif

#include <windows.h>
