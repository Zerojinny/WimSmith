// ============================================================
//  7z_wimlib (C++) - main.cpp
//  Entry point. The C++ port is made in steps:
//    1. the helper (7z_wimlib.exe --wim-helper <work folder>): done;
//       the C# GUI starts 7z_wimlib_native.exe for its jobs when that
//       file is next to it.
//    2. the window (the tabs), then this exe becomes 7z_wimlib.exe.
//  The C# version is kept in backup\cs-winforms.
// ============================================================
#include "helper.h"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // no "insert a disk" boxes of Windows for empty drives
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    std::vector<std::wstring> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        for (int n = 1; n < argc; n++) args.push_back(argv[n]);
        LocalFree(argv);
    }
    if (!args.empty() && args[0] == L"--wim-helper") return dvt::RunHelper(args);

    MessageBoxW(nullptr,
        L"This is the C++ helper of 7z_wimlib (the window is not ported yet).\n"
        L"Start 7z_wimlib.exe: it uses this file for its jobs.",
        L"7z_wimlib", MB_OK | MB_ICONINFORMATION);
    return 0;
}
