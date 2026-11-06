// ============================================================
//  7z_wimlib (C++) - main.cpp
//  Entry point. The C++ port is made in steps:
//    1. the helper (7z_wimlib.exe --wim-helper <work folder>): done;
//       the C# window starts 7z_wimlib_native.exe for its jobs when
//       that file is next to it.
//    2. the window: the frame (tabs, footer, log window, language,
//       settings) is done; the tabs follow one by one. Then this exe
//       becomes 7z_wimlib.exe.
//  The C# version is kept in backup\cs-winforms.
// ============================================================
#include "helper.h"
#include "lang.h"
#include "mainwin.h"
#include "settings.h"
#include <shellapi.h>

using namespace dvt;

// without a choice: Korean on a Korean Windows, else English
static std::wstring DefaultLanguage()
{
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN ? L"ko" : L"en";
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
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
    if (!args.empty() && args[0] == L"--wim-helper") return RunHelper(args);

    ui::Init(inst);
    L::Init(ExeDir());
    while (true)
    {
        L::Load(GetSetting(L"language", DefaultLanguage()));
        ui::InitFonts(L::FontFamily);
        if (!L::Problems.empty())
        {
            std::wstring text;
            for (const std::wstring& p : L::Problems) text += p + L"\r\n";
            MessageBoxW(nullptr, text.c_str(), L"7z_wimlib", MB_OK | MB_ICONWARNING);
        }
        MainWindow win;
        win.Create();
        // the order of the C# version; the tabs are ported one by one
        for (const wchar_t* key : { L"bk.tab_apply", L"dk.tab", L"bk.tab_capture", L"ex.tab", L"sj.tab", L"in.tab", L"br.tab" })
        {
            HWND page = win.AddPage(L::T(key));
            HWND note = ui::Label(page, L::T(L"cpp.not_ported"), ui::FontUI, ui::color::Dim);
            ui::SetLayout(page, [note](int w, int) { ui::Move(note, 0, ui::S(8), w, ui::S(40)); });
        }
        ui::Relayout(win.hwnd);
        ShowWindow(win.hwnd, SW_SHOW);
        ui::MessageLoop();
        ui::Reset();
        if (!win.restart) break;
    }
    return 0;
}
