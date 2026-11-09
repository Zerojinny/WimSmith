// ============================================================
//  WimSmith - main.cpp
//  Entry point: "WimSmith.exe --wim-helper <work folder>" runs the
//  elevated helper (helper.cpp); otherwise the window opens.
//  (The former C# / .NET version is kept in backup\cs-winforms.)
// ============================================================
#include "app.h"
#include "helper.h"
#include "lang.h"
#include "mainwin.h"
#include "settings.h"
#include <shellapi.h>
#include <objbase.h>

using namespace wimsmith;

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
    // "WimSmith.exe <file>": a WIM file opened from Explorer (assoc.cpp)
    std::wstring openFile = args.empty() ? L"" : args[0];

    // COM for the shell icons (SHGetFileInfo) and ShellExecute
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ui::Init(inst);
    L::Init(ExeDir());
    while (true)
    {
        L::Load(GetSetting(L"language", DefaultLanguage()));
        // the colors: "dark" (default) or "light"; the windows are made after this
        ui::SetTheme(!EqualsI(GetSetting(L"theme", L"dark"), L"light"));
        ui::InitFonts(L::FontFamily);
        if (!L::Problems.empty())
        {
            std::wstring text;
            for (const std::wstring& p : L::Problems) text += p + L"\r\n";
            MessageBoxW(nullptr, text.c_str(), L"WimSmith", MB_OK | MB_ICONWARNING);
        }
        // the tabs live as long as the window: one App per language
        App* app = new App();
        app->Create();
        ShowWindow(app->win.hwnd, app->win.maximize ? SW_MAXIMIZE : SW_SHOW);
        UpdateWindow(app->win.hwnd);
        app->RefreshStatus();
        // only with the first window (not again after a language or theme change)
        if (!openFile.empty()) { app->OpenFile(openFile); openFile.clear(); }
        ui::MessageLoop();
        bool again = app->win.restart;
        ui::Reset();
        // (the App and its tabs are left: a background read may still post to them)
        if (!again) break;
    }
    CoUninitialize();
    return 0;
}
