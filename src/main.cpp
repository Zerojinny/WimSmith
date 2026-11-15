// ============================================================
//  WimSmith - main.cpp
//  Entry point: "WimSmith.exe --wim-helper <work folder>" runs the
//  elevated helper (helper.cpp); otherwise the window opens.
//  (The former C# / .NET version is kept in backup\cs-winforms.)
// ============================================================
#include "app.h"
#include "extractwim.h"
#include "helper.h"
#include "lang.h"
#include "mainwin.h"
#include "makewim.h"
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
    // "WimSmith.exe --make-wim <one|each|open> <folder>...": the right-click menu of folders,
    // one process per selected folder; the first one opens the window, the
    // others give it their folder and end (makewim.cpp)
    bool makeWim = !args.empty() && args[0] == L"--make-wim";
    std::vector<std::wstring> makeWimFolders;
    std::wstring makeWimMode = L"open";
    if (makeWim)
    {
        size_t first = 1;
        if (args.size() > 1 && MakeWimMode(args[1])) { makeWimMode = args[1]; first = 2; }
        for (size_t n = first; n < args.size(); n++)
        {
            std::wstring full = FullPath(args[n]);
            makeWimFolders.push_back(full.empty() ? args[n] : full);
        }
        if (MakeWimForward(makeWimFolders, makeWimMode)) return 0;
    }
    // "WimSmith.exe --extract <one|each> <file>...": Extract / Extract each of
    // the right-click menu of WIM files, gathered the same way (extractwim.cpp)
    bool extract = args.size() >= 2 && args[0] == L"--extract" && ExtractMode(args[1]);
    std::vector<std::wstring> extractFiles;
    if (extract)
    {
        for (size_t n = 2; n < args.size(); n++)
        {
            std::wstring full = FullPath(args[n]);
            extractFiles.push_back(full.empty() ? args[n] : full);
        }
        if (ExtractForward(extractFiles, args[1])) return 0;
    }
    // "WimSmith.exe <file>": a WIM file opened from Explorer (assoc.cpp);
    // "WimSmith.exe --open <tab> <file>": from the right-click menu
    std::wstring openFile, openTab;
    if (!makeWim && !extract && args.size() >= 3 && args[0] == L"--open") { openTab = args[1]; openFile = args[2]; }
    else if (!makeWim && !extract && !args.empty()) openFile = args[0];
    // a relative path typed in the tabs is relative to the folder of WimSmith.exe
    // (a WimSmith on a USB drive with its WIM files next to it); the file given
    // on the command line is relative to the folder it was started from
    if (!openFile.empty()) { std::wstring full = FullPath(openFile); if (!full.empty()) openFile = full; }
    SetCurrentDirectoryW(ExeDir().c_str());

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
        // "Make a WIM file" of the folder menu: no main window, only its own
        if (extract)
        {
            int rc = RunExtract(extractFiles, args[1]);
            CoUninitialize();
            return rc;
        }
        if (makeWim)
        {
            int rc = RunMakeWim(makeWimFolders, makeWimMode);
            CoUninitialize();
            return rc;
        }
        // the tabs live as long as the window: one App per language
        App* app = new App();
        app->Create();
        ShowWindow(app->win.hwnd, app->win.maximize ? SW_MAXIMIZE : SW_SHOW);
        UpdateWindow(app->win.hwnd);
        app->RefreshStatus();
        // only with the first window (not again after a language or theme change)
        if (!openFile.empty()) { app->OpenFile(openFile, openTab); openFile.clear(); }
        ui::MessageLoop();
        bool again = app->win.restart;
        ui::Reset();
        // (the App and its tabs are left: a background read may still post to them)
        if (!again) break;
    }
    CoUninitialize();
    return 0;
}
