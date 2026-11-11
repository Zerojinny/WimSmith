// ============================================================
//  WimSmith - ramboot.cpp
//  Boot once from a WIM file without the main window (ramboot.h).
// ============================================================
#include "ramboot.h"
#include "app.h"
#include "lang.h"
#include "libs.h"
#include "settings.h"
#include "ui.h"
#include "wim.h"
#include <shellapi.h>
#include <winternl.h>

namespace wimsmith
{
    using namespace ui;

    wstring RunShutdown(bool restart)
    {
        wstring args = wstring(restart ? L"/r" : L"/s") + L" /t 0 /d p:0:0";
        try
        {
            std::string out, err;
            int rc = RunProcess(Combine(SystemDir(), L"shutdown.exe"), args, out, err);
            if (rc != 0) return Trim(FromCodePage(out + err, CP_OEMCP)) + L" (" + Num(rc) + L")";
            return wstring();
        }
        catch (const HelperError& e) { return e.msg; }
    }

    // boot.sdi copied by the helper, deleted after the PC has started again
    // (ramboot.delete_sdi = 1): a value under Run of the user starts
    // "WimSmith.exe --ramboot-cleanup <sdi> <boot time>" at each logon until
    // the boot time differs, i.e. the PC has restarted (the one-time start
    // from the WIM is then over). A logoff / logon or a fast startup keeps
    // the boot time, so the file stays until a real restart.
    static const wchar_t* RunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static const wchar_t* CleanupValue = L"WimSmith.DeleteBootSdi";

    // the time this Windows started (100 ns since 1601), 0 = unknown
    static unsigned long long BootTime()
    {
        struct TimeOfDay { LARGE_INTEGER BootTime, CurrentTime, TimeZoneBias; ULONG TimeZoneId, Reserved; ULONGLONG BootTimeBias, SleepTimeBias; };
        typedef LONG(NTAPI* QuerySystemInformation)(int, void*, ULONG, ULONG*);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        QuerySystemInformation query = ntdll ? (QuerySystemInformation)GetProcAddress(ntdll, "NtQuerySystemInformation") : nullptr;
        TimeOfDay t = {};
        if (!query || query(3 /* SystemTimeOfDayInformation */, &t, sizeof t, nullptr) < 0) return 0;
        return (unsigned long long)t.BootTime.QuadPart;
    }

    bool DeleteSdiAfterBoot() { return GetSetting(L"ramboot.delete_sdi", L"0") == L"1"; }

    static void ScheduleSdiCleanup(const wstring& sdi)
    {
        unsigned long long boot = BootTime();
        if (boot == 0) return;
        wchar_t self[MAX_PATH * 2];
        GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
        wstring cmd = L"\"" + wstring(self) + L"\" --ramboot-cleanup \"" + sdi + L"\" " + std::to_wstring(boot);
        RegSetKeyValueW(HKEY_CURRENT_USER, RunKey, CleanupValue, REG_SZ, cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    }

    int RunSdiCleanup(const wstring& sdi, const wstring& bootTime)
    {
        unsigned long long then = wcstoull(bootTime.c_str(), nullptr, 10), now = BootTime();
        if (now == 0) return 1;
        // the same start (some seconds apart when the clock was set): later again
        unsigned long long diff = now > then ? now - then : then - now;
        if (then != 0 && diff < 10ULL * 10000000ULL) return 0;
        // only a file named boot.sdi; it may be gone already
        if (EqualsI(FileName(sdi), L"boot.sdi") && FileExists(sdi)) DeleteFileW(sdi.c_str());
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RunKey, CleanupValue);
        return 0;
    }

    // the image to start: a list when the file has more than one (the boot image first chosen)
    static int ChooseBootImage(const wstring& wim, const wl::WimFileInfo& f)
    {
        HWND d = Dialog(L::T(L"rb.title"), 520, 160);
        HWND l = Label(d, L::F(L"rb.choose", wim));
        HWND cbo = Combo(d);
        int sel = 0;
        for (size_t n = 0; n < f.Images.size(); n++)
        {
            const wl::WimImage& im = f.Images[n];
            wstring text = Num(im.Index) + L": " + im.Name;
            if (!im.InstallationType.empty()) text += L" (" + im.InstallationType + L")";
            if (im.Index == f.BootIndex) { text += L"  [" + L::T(L"rb.boot_image") + L"]"; sel = (int)n; }
            ComboAdd(cbo, text);
        }
        ComboSelect(cbo, sel);
        HWND ok = Button(d, L"OK"), cancel = Button(d, L::T(L"bk.btn_cancel"));
        Primary(ok, false);
        SetLayout(d, [=](int cw, int ch)
        {
            Move(l, S(14), S(12), cw - S(28), S(40));
            Move(cbo, S(14), S(56), cw - S(28), S(24));
            int by = ch - S(14) - S(28);
            Move(ok, cw - S(14) - S(86) * 2 - S(8), by, S(86), S(28));
            Move(cancel, cw - S(14) - S(86), by, S(86), S(28));
        });
        Relayout(d);
        int chosen = 0;
        auto take = [&, d] { int i = ComboIndex(cbo); if (i >= 0) chosen = f.Images[i].Index; EndModal(d, 1); };
        OnClick(ok, take);
        OnClick(cancel, [d] { EndModal(d, 0); });
        OnKeys(d, take, [d] { EndModal(d, 0); });
        SetFocus(cbo);
        return RunModal(d) == 1 ? chosen : 0;
    }

    // the elevated helper with job.ini in a new work folder; a small window while
    // it runs. false = not started (UAC cancelled: nothing to tell)
    static bool RunElevated(const Job& values, wstring& workDir, DWORD& code)
    {
        wchar_t tmp[MAX_PATH + 1];
        GetTempPathW(MAX_PATH + 1, tmp);
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[32];
        swprintf_s(stamp, L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        workDir = Combine(Combine(tmp, L"WimSmith_job"), stamp);
        CreateDirs(workDir);
        wstring text;
        for (const auto& kv : values) text += kv.first + L" = " + kv.second + L"\r\n";
        if (!WriteBytes(Combine(workDir, L"job.ini"), ToUtf8(text))) throw HelperError{ L"job.ini: " + ErrorMessage(GetLastError()) };

        wchar_t self[MAX_PATH * 2];
        GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
        wstring args = L"--wim-helper \"" + workDir + L"\"";
        SHELLEXECUTEINFOW sei = { sizeof sei };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = L"runas";
        sei.lpFile = self;
        sei.lpParameters = args.c_str();
        sei.lpDirectory = workDir.c_str();
        sei.nShow = SW_HIDE;
        if (!ShellExecuteExW(&sei) || !sei.hProcess)
        {
            DWORD e = GetLastError();
            DeleteTree(workDir);
            if (e == ERROR_CANCELLED) return false;
            throw HelperError{ ErrorMessage(e) };
        }
        HANDLE process = sei.hProcess;
        HWND d = Dialog(L::T(L"rb.title"), 420, 90);
        SetWindowLongPtrW(d, GWL_EXSTYLE, GetWindowLongPtrW(d, GWL_EXSTYLE) | WS_EX_APPWINDOW);
        HWND l = Label(d, L::T(L"rb.working"));
        HWND bar = Progress(d);
        Move(l, S(14), S(14), S(392), S(24));
        Move(bar, S(14), S(50), S(392), S(6));
        ProgressMarquee(bar, true);
        ui::SetTimer(d, 1, 250, [d, process] { if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) EndModal(d, 1); });
        RunModal(d);
        ui::KillTimer(d, 1);
        DestroyWindow(d);
        WaitForSingleObject(process, INFINITE);   // the window closed early: the job still ends
        code = (DWORD)-1;
        GetExitCodeProcess(process, &code);
        CloseHandle(process);
        return true;
    }

    // status.txt without the progress lines (the last ones)
    static wstring StatusText(const wstring& workDir)
    {
        std::string bytes;
        if (!ReadBytes(Combine(workDir, L"status.txt"), bytes)) return wstring();
        std::vector<wstring> lines;
        for (const wstring& l : SplitLines(FromUtf8(bytes)))
            if (!l.empty() && l.compare(0, 2, L"P ") != 0) lines.push_back(l.compare(0, 5, L"EXIT ") == 0 ? l.substr(5) : l);
        wstring r;
        for (size_t n = lines.size() > 12 ? lines.size() - 12 : 0; n < lines.size(); n++) r += lines[n] + L"\r\n";
        return TrimEnd(r);
    }

    int RunOneTimeBoot(const wstring& wim)
    {
        if (!EqualsI(FileExt(wim), L".wim")) { Info(L::F(L"rb.not_wim", wim), MB_ICONWARNING); return 1; }
        wstring root = Root(wim);
        UINT type = root.size() == 3 ? DriveTypeOf(root[0]) : DRIVE_REMOTE;
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) { Info(L::F(L"rb.not_local", wim), MB_ICONWARNING); return 1; }

        wstring exeDir = ExeDir(), lib = Combine(exeDir, L"lib");
        libs::Migrate(exeDir);
        wstring err = libs::EnsureWimlib(exeDir);
        if (err.empty()) err = wl::Load(lib, false);
        if (!err.empty()) { Info(L::F(L"bk.no_wimlib", err, lib), MB_ICONWARNING); return 1; }
        wl::WimFileInfo f;
        int rc = wl::ReadInfo(wim, f);
        if (rc != 0) { Info(L::F(L"bk.read_failed", wim, wl::ErrorText(rc)), MB_ICONWARNING); return 1; }
        if (f.TotalParts > 1) { Info(L::F(L"rb.split", wim), MB_ICONWARNING); return 1; }
        if (f.Images.empty()) { Info(L::F(L"rb.no_image", wim), MB_ICONWARNING); return 1; }
        int index = f.Images.size() == 1 ? f.Images[0].Index : ChooseBootImage(wim, f);
        if (index <= 0) return 0;
        const wl::WimImage* im = nullptr;
        for (const wl::WimImage& i : f.Images) if (i.Index == index) im = &i;
        if (!im) return 1;
        // only Windows PE starts from a RAM disk; another image may still be one
        if (!EqualsI(im->InstallationType, L"WindowsPE")
            && !YesNo(L::F(L"rb.not_pe", index, im->Name, im->InstallationType.empty() ? wstring(L"-") : im->InstallationType), MB_ICONWARNING))
            return 0;
        bool setBoot = f.BootIndex != index;
        long long size = f.FileBytes > 0 ? f.FileBytes : FileSize(wim);
        wstring sdi = Combine(DirName(wim), L"boot.sdi");
        wstring text = L::F(L"rb.confirm", wim, index, im->Name, App::Gb(size));
        if (setBoot) text += L"\r\n\r\n" + (f.BootIndex > 0 ? L::F(L"rb.confirm_set_boot", f.BootIndex, index) : L::F(L"rb.confirm_set_boot_none", index));
        bool copySdi = !FileExists(sdi);
        if (copySdi) text += L"\r\n\r\n" + L::F(L"rb.confirm_sdi", sdi) + (DeleteSdiAfterBoot() ? L"\r\n" + L::T(L"rb.confirm_sdi_delete") : wstring());
        if (!YesNo(text, MB_ICONQUESTION)) return 0;

        wstring workDir;
        DWORD code = 0;
        try
        {
            if (!RunElevated({ { L"mode", L"ramboot" }, { L"wim", wim }, { L"index", Num(index) },
                { L"set_boot", setBoot ? L"1" : L"0" }, { L"name", im->Name }, { L"tools", lib }, { L"root", exeDir } }, workDir, code))
                return 0;
        }
        catch (const HelperError& e) { Info(L::F(L"rb.failed", wstring(L"-"), e.msg, workDir), MB_ICONERROR); return 1; }
        if (code != 0)
        {
            Info(L::F(L"rb.failed", (long long)(int)code, StatusText(workDir), workDir), MB_ICONERROR);
            return (int)code;
        }
        try
        {
            DeleteTree(workDir);
            wstring parent = DirName(workDir);
            if (!DirHasEntries(parent)) RemoveDirectoryW(parent.c_str());
        }
        catch (const HelperError&) {}
        if (copySdi && FileExists(sdi) && DeleteSdiAfterBoot()) ScheduleSdiCleanup(sdi);
        if (YesNo(L::F(L"rb.done", wim), MB_ICONQUESTION))
        {
            wstring e = RunShutdown(true);
            if (!e.empty()) Info(L::F(L"bk.after_failed", L::T(L"bk.after_restart"), e), MB_ICONERROR);
        }
        return 0;
    }
}
