// ============================================================
//  WimSmith - app.cpp
//  The shared part of the tabs and the job (see app.h).
// ============================================================
#include "app.h"
#include "helper.h"
#include "lang.h"
#include "libs.h"
#include "settings.h"
#include "tab_about.h"
#include "tab_backup.h"
#include "tab_disk.h"
#include "tab_export.h"
#include "tab_browse.h"
#include <shlwapi.h>
#include "tab_info.h"
#include "tab_splitjoin.h"
#include "version.h"
#include "tab_restore.h"
#include <shellapi.h>
#include <algorithm>

namespace wimsmith
{
    using namespace ui;

    static const wchar_t* const DefaultWimName = L"backup.wim";
    static const UINT_PTR PollTimer = 100;

    void App::Create()
    {
        root = ExeDir();
        libs::Migrate(root);                  // libwim-15.dll of pe_tools\ -> lib\ (once)
        win.Create();
        win.busy = [this] { return Busy(); };
        OnClick(win.btnCancel, [this] { CancelJob(); });

        // the order of the tabs; the ones not ported yet say so
        HWND page = win.AddPage(L::T(L"bk.tab_apply"));
        restore = new RestoreTab(*this, page);
        disk = new DiskTab(*this, win.AddPage(L::T(L"dk.tab")));
        backup = new BackupTab(*this, win.AddPage(L::T(L"bk.tab_capture")));
        exportTab = new ExportTab(*this, win.AddPage(L::T(L"ex.tab")));
        splitJoin = new SplitJoinTab(*this, win.AddPage(L::T(L"sj.tab")));
        infoTab = new InfoTab(*this, win.AddPage(L::T(L"in.tab")));
        browse = new BrowseTab(*this, win.AddPage(L::T(L"br.tab")));
        about = new AboutTab(*this, win.AddPage(L::T(L"ab.tab")));
        Log(L"WimSmith " WS_VERSION_W L" - " WS_COPYRIGHT_W);
        Log(L::T(L"bk.note"));
        libs::FetchMissingLicenses(root);     // license texts of wimlib, when missing (background)
        Relayout(win.hwnd);
    }

    void App::RefreshStatus()
    {
        restore->Refresh();
        backup->Refresh();
        exportTab->Refresh();
        splitJoin->Refresh();
        infoTab->Refresh();
        browse->SetDefault(DefaultWim());
    }

    // ---------- shared ----------

    wstring App::DefaultWim() const
    {
        wstring dir = GetSetting(L"image_folder", L"");
        return dir.empty() ? wstring() : Combine(dir, DefaultWimName);
    }

    wstring App::ShortPath(const wstring& path) const
    {
        wstring r = WithSlash(root);
        return StartsWithI(path, r) ? path.substr(r.size()) : path;
    }

    bool App::LoadWimlib()
    {
        // lib\libwim-15.dll: downloaded when missing
        wstring err = libs::EnsureWimlib(root);
        if (err.empty()) err = wl::Load(ToolDir(), false);
        if (err.empty()) { if (about) about->Refresh(); return true; }   // the wimlib line of the "?" tab
        Log(err);
        Info(L::F(L"bk.no_wimlib", err, ToolDir()), MB_ICONWARNING);
        return false;
    }

    bool App::Images(const wstring& wim, bool show, std::vector<wl::WimImage>& list)
    {
        list.clear();
        if (!LoadWimlib()) return false;
        int total = 1, found = 1;
        int rc = wl::ReadImages(wim, list, total, found);
        if (rc == 0) return true;
        wstring err = rc == wl::ErrPartsMissing ? L::F(L"sj.err_part_count", wim, total, found) : wl::ErrorText(rc);
        wstring text = L::F(L"bk.read_failed", wim, err), one = text;
        for (size_t p; (p = one.find(L"\r\n")) != wstring::npos;) one.replace(p, 2, L" ");
        Log(one);
        if (show) Info(text, MB_ICONWARNING);
        return false;
    }

    void App::WimWritten(const wstring& wim, bool toRestore)
    {
        wstring cur = Trim(GetText(restore->txtWim));
        if (EqualsI(cur, wim) || (toRestore && cur.empty())) { SetText(restore->txtWim, wim); restore->ReadImages(false); }
        if (EqualsI(Trim(GetText(exportTab->txtSrc)), wim)) exportTab->ReadImages(false);
        wstring info = Trim(GetText(infoTab->txtWim));
        if (EqualsI(info, wim) || (!toRestore && info.empty())) { SetText(infoTab->txtWim, wim); infoTab->ReadInfo(false); }
    }

    void App::OpenFile(const wstring& file, const wstring& tabGiven)
    {
        wstring wim = FullPath(file);
        if (!FileExists(wim)) { Log(L::F(L"st.open_missing", file)); return; }
        wstring tab = tabGiven.empty() ? GetSetting(L"open_tab", L"browse") : tabGiven;
        if (EqualsI(tab, L"ramboot"))
        {
            OneTimeBoot(wim);
            return;
        }
        if (EqualsI(tab, L"append"))
        {
            win.SelectPage(backup->page);
            backup->SetTarget(wim);
            return;
        }
        if (EqualsI(tab, L"splitjoin"))
        {
            win.SelectPage(splitJoin->page);
            splitJoin->SetFile(wim);
            return;
        }
        if (EqualsI(tab, L"info"))
        {
            SetText(infoTab->txtWim, wim);
            win.SelectPage(infoTab->page);
            infoTab->ReadInfo(true);
        }
        else if (EqualsI(tab, L"restore"))
        {
            win.SelectPage(restore->page);
            restore->SetWim(wim, 1);
        }
        else if (EqualsI(tab, L"export"))
        {
            win.SelectPage(exportTab->page);
            exportTab->SetSource(wim, 0);
        }
        else Browse(wim, 0);
    }

    void App::Browse(const wstring& wim, int image)
    {
        win.SelectPage(browse->page);
        browse->ShowWim(wim, image);
    }

    int App::Processors()
    {
        DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        return n > 0 ? (int)n : 1;
    }

    wstring App::Gb(long long bytes)
    {
        wchar_t b[64];
        swprintf_s(b, L"%.1f GB", bytes / 1073741824.0);
        return b;
    }

    wstring App::Group(long long n)
    {
        wstring s = std::to_wstring(n < 0 ? -n : n), r;
        for (size_t i = 0; i < s.size(); i++)
        {
            if (i > 0 && (s.size() - i) % 3 == 0) r += L',';
            r += s[i];
        }
        return n < 0 ? L"-" + r : r;
    }

    std::vector<Volume> App::Volumes(bool readyOnly)
    {
        std::vector<Volume> list;
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; i++)
        {
            if (!(mask & (1u << i))) continue;
            wchar_t letter = (wchar_t)(L'A' + i);
            UINT t = DriveTypeOf(letter);
            if (t != DRIVE_FIXED && t != DRIVE_REMOVABLE) continue;
            Volume v;
            v.Letter = letter;
            v.Path = wstring(1, letter) + L":\\";
            wchar_t label[MAX_PATH + 1] = { 0 }, fs[MAX_PATH + 1] = { 0 };
            wstring info;
            ULARGE_INTEGER avail, total, free;
            if (GetVolumeInformationW(v.Path.c_str(), label, MAX_PATH + 1, nullptr, nullptr, nullptr, fs, MAX_PATH + 1)
                && GetDiskFreeSpaceExW(v.Path.c_str(), &avail, &total, &free))
            {
                v.Size = (long long)total.QuadPart;
                info = wstring(fs) + L", " + Gb(v.Size) + L", " + L::F(L"bk.vol_free", Gb((long long)free.QuadPart));
            }
            else info = L::T(L"bk.vol_not_ready");
            if (readyOnly && v.Size < 0) continue;
            v.Text = L::F(L"bk.vol_line", letter, label[0] ? wstring(label) : L::T(L"bk.vol_no_label"), info);
            list.push_back(v);
        }
        return list;
    }

    bool App::ConfirmTyped(const wstring& text, const wstring& expected, const wstring& titleKey)
    {
        HWND d = Dialog(L::T(titleKey), 420, 140);
        HWND l = Label(d, text);
        HWND t = Edit(d, ES_UPPERCASE);
        SendMessageW(t, EM_SETLIMITTEXT, std::max<size_t>(1, expected.size()), 0);
        HWND ok = Button(d, L"OK");
        HWND cancel = Button(d, L::T(L"bk.btn_cancel"));
        Enable(ok, false);
        Move(l, S(14), S(12), S(392), S(44));
        Move(t, S(14), S(60), S(60), S(24));
        Move(ok, S(226), S(100), S(86), S(28));
        Move(cancel, S(320), S(100), S(86), S(28));
        OnChange(t, [=] { Enable(ok, EqualsI(Trim(GetText(t)), expected)); });
        OnClick(ok, [d] { EndModal(d, 1); });
        OnClick(cancel, [d] { EndModal(d, 0); });
        OnKeys(d, [d, ok] { if (IsWindowEnabled(ok)) EndModal(d, 1); }, [d] { EndModal(d, 0); });
        SetFocus(t);
        return RunModal(d) == 1;
    }

    // ---------- the job ----------

    void App::SetBusy(bool busy)
    {
        for (auto& f : busyHandlers) f(busy);
        Enable(win.btnCancel, busy);
        if (!busy) { ProgressMarquee(win.progress, false); ProgressValue(win.progress, 0); }
    }

    void App::StartHelper(Job values)
    {
        values[L"tools"] = ToolDir();
        values[L"root"] = root;
        job.mode = values[L"mode"];
        // a relative path typed in a tab is relative to the exe folder (the current
        // folder of this process, main.cpp); the helper gets it in full
        for (const wchar_t* key : { L"wim", L"source", L"folder", L"swm", L"drivers", L"dism" })
        {
            auto it = values.find(key);
            if (it == values.end() || it->second.empty() || !PathIsRelativeW(it->second.c_str())) continue;
            wstring full = FullPath(it->second);
            if (!full.empty()) it->second = full;
        }
        // backup / restore / disk need administrator rights; export, split, join and verify do not;
        // update: the browse tab sends its edits here only when the file needs them;
        // modify (the info tab): admin = 1 when the user may not write the file
        bool elevate = job.mode == L"capture" || job.mode == L"apply" || job.mode == L"disk" || job.mode == L"update"
            || job.mode == L"ramboot" || values[L"admin"] == L"1";
        // the elevated helper does not see the network drives (and SUBST drives)
        // of this session: their paths go to it as \\server\share\...
        if (elevate)
            for (const wchar_t* key : { L"wim", L"source", L"folder", L"drivers", L"dism", L"tools", L"root" })
            {
                auto it = values.find(key);
                if (it == values.end()) continue;
                wstring p = SessionFreePath(it->second);
                if (p == it->second) continue;
                if (wcscmp(key, L"tools") != 0 && wcscmp(key, L"root") != 0) Log(L::F(L"bk.log_unc", it->second, p));
                it->second = p;
            }
        wchar_t tmp[MAX_PATH + 1];
        GetTempPathW(MAX_PATH + 1, tmp);
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[32];
        swprintf_s(stamp, L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        workDir = Combine(Combine(tmp, L"WimSmith_job"), stamp);
        try
        {
            CreateDirs(workDir);
            wstring text;
            for (const auto& kv : values) text += kv.first + L" = " + kv.second + L"\r\n";
            if (!WriteBytes(Combine(workDir, L"job.ini"), ToUtf8(text)))
                throw HelperError{ L"job.ini: " + ErrorMessage(GetLastError()) };
        }
        catch (const HelperError& e) { Info(e.msg, MB_ICONERROR); return; }
        statusFile = Combine(workDir, L"status.txt");
        Log(L::F(L"bk.log_work", ShortPath(workDir)));

        wchar_t selfBuf[MAX_PATH * 2];
        GetModuleFileNameW(nullptr, selfBuf, MAX_PATH * 2);
        wstring self = elevate ? SessionFreePath(selfBuf) : wstring(selfBuf);
        wstring args = L"--wim-helper \"" + workDir + L"\"";
        SHELLEXECUTEINFOW sei = { sizeof sei };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        sei.hwnd = win.hwnd;
        sei.lpVerb = elevate ? L"runas" : nullptr;
        sei.lpFile = self.c_str();
        sei.lpParameters = args.c_str();
        sei.lpDirectory = workDir.c_str();
        sei.nShow = SW_HIDE;
        if (!ShellExecuteExW(&sei) || !sei.hProcess)
        {
            DWORD e = GetLastError();
            Log(e == ERROR_CANCELLED ? L::T(L"adk.log_uac_cancelled") : ErrorMessage(e));
            win.SetStatus(L::T(L"bk.state_cancelled"));
            return;
        }
        helper = sei.hProcess;
        linesShown = 0;
        started = GetTickCount64();
        phaseText = L::T(elevate ? L"bk.phase_start" : L"ex.phase_start");
        SetBusy(true);
        ProgressMarquee(win.progress, true);
        ShowProgress();
        ui::SetTimer(win.hwnd, PollTimer, 500, [this] { Poll(); });
    }

    // stops the job at the next progress message of wimlib
    void App::CancelJob()
    {
        if (!Busy()) return;
        WriteBytes(Combine(workDir, L"cancel"), "cancel");
        Enable(win.btnCancel, false);
        Log(L::T(L"bk.log_cancelling"));
    }

    void App::ShowProgress()
    {
        ULONGLONG s = (GetTickCount64() - started) / 1000;
        win.SetStatus(L::F(L"bk.state_running", phaseText, (long long)(s / 60), (long long)(s % 60)));
    }

    // "P <percent> <phase> <numbers>" of the helper -> progress line and text
    void App::OnProgressLine(const wstring& line)
    {
        std::vector<wstring> p = Split(line, L' ');
        if (p.size() < 3) return;
        long long pctL;
        if (!TryParseLong(p[1], pctL)) return;
        int pct = (int)pctL;
        auto num = [&p](size_t i) { return i < p.size() ? ParseLong(p[i], 0) : 0; };
        long long a = num(3), b = num(4), c = num(5), d = num(6);
        int shown = std::max(0, pct);
        const wstring& ph = p[2];
        if (ph == L"scan") phaseText = L::F(L"bk.phase_scan", Group(a), Group(b), Gb(c));
        else if (ph == L"write") phaseText = L::F(L"bk.phase_write", shown, Gb(a), Gb(b));
        else if (ph == L"apply") phaseText = L::F(L"bk.phase_apply", shown, Gb(a), Gb(b));
        else if (ph == L"format") phaseText = L::T(L"bk.phase_format");
        else if (ph == L"boot") phaseText = L::T(L"bk.phase_boot");
        else if (ph == L"drivers") phaseText = L::T(L"rs.phase_drivers");
        else if (ph == L"snapshot") phaseText = L::T(L"bk.phase_snapshot");
        else if (ph == L"disk") phaseText = L::T(L"dk.phase");
        else if (ph == L"structure") phaseText = L::T(L"bk.phase_structure");
        else if (ph == L"metadata") phaseText = L::T(L"bk.phase_metadata");
        else if (ph == L"export") phaseText = L::T(L"ex.phase_export");
        else if (ph == L"update") phaseText = L::T(L"br.updating_admin");
        else if (ph == L"rebuild") phaseText = L::T(L"in.phase_rebuild");
        else if (ph == L"split") phaseText = c > 0 ? L::F(L"sj.phase_split", shown, Gb(a), Gb(b), c, d) : L::T(L"sj.phase_split_start");
        else if (ph == L"join") phaseText = L::T(L"sj.phase_join");
        else if (ph == L"vopen") phaseText = L::T(L"vf.phase_open");
        else if (ph == L"integrity") phaseText = L::F(L"vf.phase_integrity", shown, Gb(a), Gb(b));
        else if (ph == L"calcintegrity") phaseText = L::F(L"bk.phase_integrity", shown, Gb(a), Gb(b));
        else if (ph == L"vimage") phaseText = L::F(L"vf.phase_image", a, b);
        else if (ph == L"verify") phaseText = b > 0 ? L::F(L"vf.phase_verify", shown, Gb(a), Gb(b)) : L::T(L"vf.phase_open");
        if (pct >= 0)
        {
            ProgressMarquee(win.progress, false);
            ProgressValue(win.progress, std::min(100, pct));
        }
        else ProgressMarquee(win.progress, true);
    }

    // the new lines of status.txt; true + exit when "EXIT ..." came
    bool App::ReadLines(wstring& exit)
    {
        std::string bytes;
        if (statusFile.empty() || !ReadBytes(statusFile, bytes)) return false;
        std::vector<wstring> lines;
        for (const wstring& l : SplitLines(FromUtf8(bytes))) if (!l.empty()) lines.push_back(l);
        bool found = false;
        for (; linesShown < lines.size(); linesShown++)
        {
            const wstring& l = lines[linesShown];
            if (l.compare(0, 2, L"P ") == 0) OnProgressLine(l);
            else if (l.compare(0, 7, L"LETTER ") == 0 && l.size() > 7) job.newLetter = UpperChar(l[7]);
            else
            {
                if (l.compare(0, 5, L"EXIT ") == 0) { exit = l.substr(5); found = true; }
                Log(l);
            }
        }
        return found;
    }

    void App::Poll()
    {
        if (!helper) { ui::KillTimer(win.hwnd, PollTimer); return; }
        wstring exit;
        bool hasExit = ReadLines(exit);
        if (WaitForSingleObject(helper, 0) == WAIT_TIMEOUT) { ShowProgress(); return; }

        ui::KillTimer(win.hwnd, PollTimer);
        DWORD code = (DWORD)-1;
        GetExitCodeProcess(helper, &code);
        CloseHandle(helper);
        helper = nullptr;
        wstring last;
        if (ReadLines(last)) { exit = last; hasExit = true; }
        SetBusy(false);
        wstring elapsed = L::F(L"bk.elapsed", Duration(GetTickCount64() - started));
        Log(elapsed);

        if (code == 0)
        {
            DeleteWorkDir();
            win.SetStatus(L::T(L"bk.state_done") + L"  (" + elapsed + L")");
            wstring msg = onSuccess ? onSuccess(elapsed) : wstring();
            if (!msg.empty()) AfterDone(msg);
            return;
        }
        if ((int)code == wl::ErrAborted)
        {
            win.SetStatus(L::T(L"bk.state_cancelled"));
            wstring more = job.mode == L"split" ? L"\r\n\r\n" + L::T(L"sj.cancelled_split")
                : job.mode != L"apply" ? wstring()
                : L"\r\n\r\n" + L::F(job.ToFolder() ? L"bk.cancelled_apply_folder" : L"bk.cancelled_apply", job.target);
            Info(L::T(L"bk.cancelled") + more, MB_ICONWARNING);
        }
        else
        {
            win.SetStatus(L::T(L"bk.state_failed"));
            // a verification that found damage (or a backup whose verification did)
            wstring head = job.mode == L"verify" || (job.mode == L"capture" && hasExit && exit.find(L"verification failed") != wstring::npos)
                ? L::F(L"vf.failed", job.wim) + L"\r\n\r\n" : wstring();
            Info(head + L::F(L"bk.failed", (long long)(int)code, hasExit ? exit : wstring(L"-"), ShortPath(workDir)), MB_ICONERROR);
        }
        if (onFailure) onFailure();
    }

    // after a successful job: the work folder (and WimSmith_job when it is empty)
    void App::DeleteWorkDir()
    {
        if (workDir.empty() || !DirExists(workDir)) return;
        try
        {
            DeleteTree(workDir);
            Log(L::F(L"bk.log_work_deleted", ShortPath(workDir)));
            wstring parent = DirName(workDir);
            if (!DirHasEntries(parent)) RemoveDirectoryW(parent.c_str());
        }
        catch (const HelperError& e) { Log(L::F(L"bk.log_work_not_deleted", ShortPath(workDir), e.msg)); }
    }

    // "1시간 2분 3초" / "1 h 2 min 3 s"
    wstring App::Duration(ULONGLONG ms)
    {
        long long s = (long long)(ms / 1000), h = s / 3600, m = (s / 60) % 60;
        if (h > 0) return L::F(L"bk.duration_h", h, m, s % 60);
        if (m > 0) return L::F(L"bk.duration_m", m, s % 60);
        return L::F(L"bk.duration_s", s);
    }

    // ---------- after a successful job: nothing / shut down / restart ----------

    void App::AfterDone(const wstring& msg)
    {
        int action = ComboIndex(win.cboAfter);
        if (action != 1 && action != 2) { Info(msg); return; }
        wstring what = L::T(action == 1 ? L"bk.after_shutdown" : L"bk.after_restart");
        int seconds = AfterSeconds();   // [Settings], 10 s by default
        Log(L::F(L"bk.log_after", what, seconds));
        if (!Countdown(msg, action == 1 ? L"bk.after_countdown_shutdown" : L"bk.after_countdown_restart", seconds))
        {
            Log(L::F(L"bk.log_after_cancelled", what));
            return;
        }
        Shutdown(action == 2);
    }

    void App::Shutdown(bool restart)
    {
        wstring what = L::T(restart ? L"bk.after_restart" : L"bk.after_shutdown");
        // no /f: programs with unsaved work may still ask before Windows goes down
        wstring args = wstring(restart ? L"/r" : L"/s") + L" /t 0 /d p:0:0";
        try
        {
            std::string out, err;
            int rc = RunProcess(Combine(SystemDir(), L"shutdown.exe"), args, out, err);
            Log(L"shutdown " + args);
            if (rc != 0) throw HelperError{ Trim(FromCodePage(out + err, CP_OEMCP)) + L" (" + Num(rc) + L")" };
        }
        catch (const HelperError& e)
        {
            Log(e.msg);
            Info(L::F(L"bk.after_failed", what, e.msg), MB_ICONERROR);
        }
    }

    // ---------- the next start (once) from a WIM file in a RAM disk ----------

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
        OnClick(ok, [&, d] { int i = ComboIndex(cbo); if (i >= 0) chosen = f.Images[i].Index; EndModal(d, 1); });
        OnClick(cancel, [d] { EndModal(d, 0); });
        OnKeys(d, [&, d] { int i = ComboIndex(cbo); if (i >= 0) chosen = f.Images[i].Index; EndModal(d, 1); }, [d] { EndModal(d, 0); });
        SetFocus(cbo);
        return RunModal(d) == 1 ? chosen : 0;
    }

    void App::OneTimeBoot(const wstring& wim)
    {
        if (Busy()) { Info(L::T(L"rb.busy"), MB_ICONWARNING); return; }
        if (!EqualsI(FileExt(wim), L".wim")) { Info(L::F(L"rb.not_wim", wim), MB_ICONWARNING); return; }
        if (!LoadWimlib()) return;
        wl::WimFileInfo f;
        int rc = wl::ReadInfo(wim, f);
        if (rc != 0) { Info(L::F(L"bk.read_failed", wim, wl::ErrorText(rc)), MB_ICONWARNING); return; }
        if (f.TotalParts > 1) { Info(L::F(L"rb.split", wim), MB_ICONWARNING); return; }
        if (f.Images.empty()) { Info(L::F(L"rb.no_image", wim), MB_ICONWARNING); return; }
        int index = f.Images.size() == 1 ? f.Images[0].Index : ChooseBootImage(wim, f);
        if (index <= 0) return;
        const wl::WimImage* im = nullptr;
        for (const wl::WimImage& i : f.Images) if (i.Index == index) im = &i;
        if (!im) return;
        // only Windows PE starts from a RAM disk; another image may still be one
        if (!EqualsI(im->InstallationType, L"WindowsPE")
            && !YesNo(L::F(L"rb.not_pe", index, im->Name, im->InstallationType.empty() ? wstring(L"-") : im->InstallationType), MB_ICONWARNING))
            return;
        bool setBoot = f.BootIndex != index;
        long long size = f.FileBytes > 0 ? f.FileBytes : FileSize(wim);
        wstring text = L::F(L"rb.confirm", wim, index, im->Name, Gb(size));
        if (setBoot) text += L"\r\n\r\n" + (f.BootIndex > 0 ? L::F(L"rb.confirm_set_boot", f.BootIndex, index) : L::F(L"rb.confirm_set_boot_none", index));
        if (!FileExists(Combine(DirName(wim), L"boot.sdi"))) text += L"\r\n\r\n" + L::F(L"rb.confirm_sdi", Combine(DirName(wim), L"boot.sdi"));
        if (!YesNo(text, MB_ICONQUESTION)) return;

        job = JobInfo();
        job.mode = L"ramboot";
        job.wim = wim;
        job.image = index;
        onFailure = nullptr;
        onSuccess = [this, wim, setBoot](const wstring&) -> wstring
        {
            if (setBoot) WimWritten(wim, false);
            Log(L::F(L"rb.done_log", wim));
            if (YesNo(L::F(L"rb.done", wim), MB_ICONQUESTION)) Shutdown(true);
            return wstring();   // no "when done"
        };
        Log(L::F(L"rb.log_start", wim, index));
        StartHelper({ { L"mode", L"ramboot" }, { L"wim", wim }, { L"index", Num(index) },
            { L"set_boot", setBoot ? L"1" : L"0" }, { L"name", im->Name } });
    }

    // the done message with a countdown; true = go on (time up or [Now]), false = cancelled
    bool App::Countdown(const wstring& msg, const wstring& countdownKey, int seconds)
    {
        HWND d = Dialog(L::T(L"bk.after_title"), 520, 260);
        SetWindowLongPtrW(d, GWL_EXSTYLE, GetWindowLongPtrW(d, GWL_EXSTYLE) | WS_EX_APPWINDOW);
        SetWindowPos(d, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        HWND l = Label(d, msg);
        HWND c = Label(d, L"", FontBold, color::Danger);
        HWND now = Button(d, L::T(L"bk.after_now"));
        HWND cancel = Button(d, L::T(L"bk.btn_cancel"));
        Move(l, S(14), S(12), S(492), S(150));
        Move(c, S(14), S(170), S(492), S(40));
        Move(now, S(300), S(220), S(100), S(28));
        Move(cancel, S(406), S(220), S(100), S(28));
        ULONGLONG end = GetTickCount64() + seconds * 1000ULL;
        auto show = [=]
        {
            ULONGLONG t = GetTickCount64();
            int left = t >= end ? 0 : (int)((end - t + 999) / 1000);
            SetText(c, L::F(countdownKey, left));
            if (left == 0) EndModal(d, 1);
        };
        show();
        ui::SetTimer(d, 1, 250, show);
        OnClick(now, [d] { EndModal(d, 1); });
        OnClick(cancel, [d] { EndModal(d, 0); });
        // Enter must not shut down
        OnKeys(d, [d] { EndModal(d, 0); }, [d] { EndModal(d, 0); });
        SetFocus(cancel);
        return RunModal(d) == 1;
    }
}
