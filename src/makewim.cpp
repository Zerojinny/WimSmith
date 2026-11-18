// ============================================================
//  WimSmith - makewim.cpp
//  "WimSmith > Make a WIM file" of the right-click menu of folders,
//  without the main window (makewim.h).
// ============================================================
#include "makewim.h"
#include "app.h"
#include "gather.h"
#include "helper.h"
#include "lang.h"
#include "libs.h"
#include "settings.h"
#include "ui.h"
#include "wim.h"
#include <shellapi.h>
#include <commctrl.h>

namespace wimsmith
{
    using namespace ui;

    static const int CompTypes[] = { wl::CompressionLzx, wl::CompressionXpress, wl::CompressionLzms };

    bool MakeWimMode(const wstring& s) { return s == L"one" || s == L"each" || s == L"open"; }

    // one window per mode: the folders of one menu click go to the window of that item (gather.h)
    bool MakeWimForward(const std::vector<wstring>& folders, const wstring& mode)
    {
        return gather::Forward(L"MakeWim." + mode, folders);
    }

    // ---------- paths ----------

    // a full path without the last backslash (a root keeps it)
    static wstring CleanFolder(const wstring& f)
    {
        wstring p = FullPath(Trim(f));
        while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
        return p;
    }

    // the image name / file name of a folder (a drive: its letter)
    static wstring NameOf(const wstring& folder)
    {
        wstring n = FileName(folder);
        if (n.empty() && !folder.empty()) n = wstring(1, UpperChar(folder[0]));
        return n;
    }

    // one folder: <folder>.wim next to it; several of one parent: <parent>\<parent name>.wim
    static wstring DefaultFile(const std::vector<wstring>& folders)
    {
        if (folders.empty()) return wstring();
        wstring parent = DirName(folders[0]);
        bool same = true;
        for (const wstring& f : folders) if (!EqualsI(DirName(f), parent)) same = false;
        if (folders.size() > 1 && same && !FileName(parent).empty()) return Combine(parent, FileName(parent) + L".wim");
        return Combine(parent, NameOf(folders[0]) + L".wim");
    }

    // ---------- the helper ----------

    static wstring NewWorkDir()
    {
        wchar_t tmp[MAX_PATH + 1];
        GetTempPathW(MAX_PATH + 1, tmp);
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[48];
        swprintf_s(stamp, L"%04d%02d%02d-%02d%02d%02d-%u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
        return Combine(Combine(tmp, L"WimSmith_job"), stamp);
    }

    // the elevated helper with job.ini in workDir; nullptr = UAC cancelled
    static HANDLE StartElevated(const Job& values, const wstring& workDir, HWND owner)
    {
        CreateDirs(workDir);
        wstring text;
        for (const auto& kv : values) text += kv.first + L" = " + kv.second + L"\r\n";
        if (!WriteBytes(Combine(workDir, L"job.ini"), ToUtf8(text))) throw HelperError{ L"job.ini: " + ErrorMessage(GetLastError()) };
        wchar_t self[MAX_PATH * 2];
        GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
        wstring exe = SessionFreePath(self);
        wstring args = L"--wim-helper \"" + workDir + L"\"";
        SHELLEXECUTEINFOW sei = { sizeof sei };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        sei.hwnd = owner;
        sei.lpVerb = L"runas";
        sei.lpFile = exe.c_str();
        sei.lpParameters = args.c_str();
        sei.lpDirectory = workDir.c_str();
        sei.nShow = SW_HIDE;
        if (ShellExecuteExW(&sei) && sei.hProcess) return sei.hProcess;
        DWORD e = GetLastError();
        try { DeleteTree(workDir); } catch (const HelperError&) {}
        if (e == ERROR_CANCELLED) return nullptr;
        throw HelperError{ ErrorMessage(e) };
    }

    static void DeleteWorkDir(const wstring& workDir)
    {
        try
        {
            DeleteTree(workDir);
            wstring parent = DirName(workDir);
            if (!DirHasEntries(parent)) RemoveDirectoryW(parent.c_str());
        }
        catch (const HelperError&) {}
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

    static wstring Clock(ULONGLONG ms)
    {
        ULONGLONG s = ms / 1000;
        return Num((long long)(s / 60)) + (s % 60 < 10 ? L":0" : L":") + Num((long long)(s % 60));
    }

    // ---------- the window ----------

    int RunMakeWim(const std::vector<wstring>& given, const wstring& mode)
    {
        std::vector<wstring> folders;
        auto addFolders = [&folders](const std::vector<wstring>& list)
        {
            for (const wstring& f : list)
            {
                wstring c = CleanFolder(f);
                if (c.empty() || !DirExists(c)) continue;
                bool known = false;
                for (const wstring& k : folders) if (EqualsI(k, c)) known = true;
                if (!known) folders.push_back(c);
            }
        };
        addFolders(given);

        HWND d = Dialog(L::T(L"mw.title"), 660, 470);
        SetWindowLongPtrW(d, GWL_EXSTYLE, GetWindowLongPtrW(d, GWL_EXSTYLE) | WS_EX_APPWINDOW);
        HWND lblFolders = Label(d, L"");
        HWND btnAdd = Button(d, L::T(L"mw.btn_add"));
        HWND btnRemove = Button(d, L::T(L"mw.btn_remove"));
        HWND list = List(d, { { L::T(L"mw.col_folder"), 340 }, { L::T(L"mw.col_saved"), 260 } }, 0);
        Grid grid(d);
        grid.labelWidth = 120;
        HWND rbOne = Radio(d, L::T(L"mw.one_file"), 1);
        HWND rbEach = Radio(d, L::T(L"mw.each_file"), 1);
        SetChecked(rbOne, true);
        grid.Add(L::T(L"mw.row_mode"), { Box(rbOne), Box(rbEach) });
        HWND txtTarget = Edit(d);
        HWND btnTarget = Button(d, L::T(L"qbt.btn_browse"));
        Item fill = Fill(txtTarget);
        fill.right = 6 + 8 + 100;   // the button is placed next to it (layout)
        grid.Add(L::T(L"bk.row_wim"), { fill });
        HWND lblTarget = grid.rows.back().label;
        HWND cboComp = Combo(d);
        for (const wchar_t* k : { L"bk.comp_lzx", L"bk.comp_xpress", L"bk.comp_lzms" }) ComboAdd(cboComp, L::T(k));
        ComboSelect(cboComp, 0);
        HWND lblThreads = Label(d, L::T(L"bk.row_threads"));
        HWND numThreads = Number(d, 0, 64, 0);   // 0 = automatic
        grid.Add(L::T(L"bk.row_comp"), { Field(cboComp, 280), Txt(lblThreads, 8, 4), Field(numThreads, 60) });
        HWND chkSnapshot = Check(d, L::T(L"mw.snapshot"));
        grid.Add(L::T(L"bk.row_snapshot"), { Box(chkSnapshot) });
        HWND chkVerify = Check(d, L::T(L"mw.verify"));
        HWND chkCheck = Check(d, L::T(L"mw.check"));
        grid.Add(L::T(L"vf.row"), { Box(chkVerify), Box(chkCheck) });
        HWND lblState = Label(d, L"", FontUI, color::Dim);
        HWND bar = ui::Progress(d);
        Show(bar, false);
        HWND btnStart = Button(d, L::T(L"mw.btn_start"));
        HWND btnClose = Button(d, L::T(L"mw.btn_close"));
        Primary(btnStart, false);

        SetLayout(d, [=, &grid](int cw, int ch)
        {
            int x = S(14), w = cw - S(28);
            int by = ch - S(14) - S(28);
            Move(btnClose, cw - S(14) - S(96), by, S(96), S(28));
            Move(btnStart, cw - S(14) - S(96) - S(8) - S(110), by, S(110), S(28));
            Move(lblState, x, by + S(6), std::max(S(40), w - S(96) - S(110) - S(16)), S(20));
            Move(bar, x, by - S(16), w, S(6));
            int gy = by - S(22) - S(grid.rowHeight) * (int)grid.rows.size();
            grid.Place(S(8), gy, cw - S(16));
            RECT r;
            GetWindowRect(txtTarget, &r);
            MapWindowPoints(nullptr, d, (POINT*)&r, 2);
            Move(btnTarget, r.right + S(8), r.top - S(3), S(100), S(28));
            Move(lblFolders, x, S(14), std::max(S(40), w - S(96) * 2 - S(16)), S(20));
            Move(btnAdd, cw - S(14) - S(96) * 2 - S(8), S(8), S(96), S(28));
            Move(btnRemove, cw - S(14) - S(96), S(8), S(96), S(28));
            int ly = S(44);
            Move(list, x, ly, w, std::max(S(60), gy - S(6) - ly));
        });

        bool each = false, running = false, asking = false;
        // one / each: [Start] by itself once no folder came in for a second
        bool autoStart = mode != L"open";
        ULONGLONG lastFolder = GetTickCount64();
        wstring autoTarget[2], typed[2];   // [0] the WIM file, [1] the folder of one WIM file per folder
        wstring linkChecked;               // the target whose link was checked (a warning once per target)
        wstring workDir;
        HANDLE process = nullptr;
        std::vector<wstring> written;
        ULONGLONG started = 0;

        auto fileOf = [&](size_t i, const wstring& target) { return each ? Combine(target, NameOf(folders[i]) + L".wim") : target; };
        auto fillList = [&]
        {
            SetText(lblFolders, L::F(L"mw.folders", (long long)folders.size()));
            wstring target = Trim(GetText(txtTarget));
            ListClear(list);
            for (size_t i = 0; i < folders.size(); i++)
                ListAdd(list, { folders[i], each ? fileOf(i, target) : NameOf(folders[i]) });
        };
        // the default target follows the folders until the user types another one
        auto updateTarget = [&]
        {
            int m = each ? 1 : 0;
            wstring def = each ? (folders.empty() ? wstring() : DirName(folders[0])) : DefaultFile(folders);
            wstring cur = Trim(GetText(txtTarget));
            if (cur.empty() || EqualsI(cur, autoTarget[m])) SetText(txtTarget, def);
            autoTarget[m] = def;
            fillList();
        };
        auto setMode = [&](bool toEach)
        {
            if (toEach == each) return;
            typed[each ? 1 : 0] = Trim(GetText(txtTarget));
            each = toEach;
            SetText(lblTarget, L::T(each ? L"mw.row_folder" : L"bk.row_wim"));
            SetText(txtTarget, typed[each ? 1 : 0]);
            updateTarget();
        };
        auto busy = [&]
        {
            for (HWND c : { btnAdd, btnRemove, list, rbOne, rbEach, txtTarget, btnTarget, cboComp, numThreads, chkSnapshot, chkVerify, chkCheck, btnStart })
                Enable(c, !running);
            Enable(btnClose, true);
            SetText(btnClose, L::T(running ? L"bk.btn_cancel" : L"mw.btn_close"));
        };
        auto cancel = [&]
        {
            if (!running) return;
            WriteBytes(Combine(workDir, L"cancel"), "cancel");
            Enable(btnClose, false);
            SetText(lblState, L::T(L"bk.log_cancelling"));
        };

        std::function<void()> poll = [&]
        {
            std::string bytes;
            wstring prefix, phase;
            int pct = -1;
            if (ReadBytes(Combine(workDir, L"status.txt"), bytes))
                for (const wstring& l : SplitLines(FromUtf8(bytes)))
                {
                    if (l.compare(0, 2, L"P ") != 0) continue;
                    std::vector<wstring> p = Split(l, L' ');
                    if (p.size() >= 5 && p[2] == L"source") { prefix = L::F(L"mw.phase_source", p[3], p[4]); continue; }
                    int v;
                    wstring t = App::PhaseText(l, v);
                    if (!t.empty()) phase = t;
                    pct = v;
                }
            if (IsWindowEnabled(btnClose))
            {
                wstring what = (prefix.empty() ? wstring() : prefix + L"  ") + (phase.empty() ? L::T(L"bk.phase_start") : phase);
                ULONGLONG s = (GetTickCount64() - started) / 1000;
                SetText(lblState, L::F(L"bk.state_running", what, (long long)(s / 60), (long long)(s % 60)));
            }
            if (pct >= 0)
            {
                ProgressMarquee(bar, false);
                ProgressValue(bar, std::min(100, pct));
            }
            else ProgressMarquee(bar, true);
            if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return;

            ui::KillTimer(d, 1);
            DWORD code = (DWORD)-1;
            GetExitCodeProcess(process, &code);
            CloseHandle(process);
            process = nullptr;
            running = false;
            busy();
            Show(bar, false);
            if (code == 0)
            {
                DeleteWorkDir(workDir);
                wstring files;
                for (const wstring& f : written) files += f + L"\r\n";
                SetText(lblState, L::T(L"mw.state_done"));
                Info(L::F(L"mw.done", TrimEnd(files), Clock(GetTickCount64() - started)));
                EndModal(d, 1);
                return;
            }
            if ((int)code == wl::ErrAborted)
            {
                SetText(lblState, L::T(L"bk.state_cancelled"));
                Info(L::F(L"mw.cancelled", workDir));
                return;
            }
            SetText(lblState, L::F(L"mw.state_failed", (long long)(int)code));
            Info(L::F(L"mw.failed", (long long)(int)code, StatusText(workDir), workDir), MB_ICONERROR);
        };

        std::function<void(bool)> start = [&](bool confirm)
        {
            if (autoStart) { autoStart = false; ui::KillTimer(d, 2); SetText(lblState, L""); }
            if (running || asking) return;
            if (folders.empty()) { Info(L::T(L"mw.no_folders"), MB_ICONWARNING); return; }
            wstring target = FullPath(Trim(GetText(txtTarget)));
            if (target.empty()) { Info(L::T(each ? L"mw.err_folder" : L"bk.err_wim_name"), MB_ICONWARNING); return; }
            bool snapshot = Checked(chkSnapshot);
            std::vector<wstring> files;
            for (size_t i = 0; i < folders.size(); i++) files.push_back(fileOf(i, target));
            for (size_t i = 0; i < folders.size(); i++)
            {
                wstring why = CaptureProblem(folders[i], files[i], snapshot);
                // no WIM file inside any of the folders
                for (const wstring& other : folders)
                    if (why.empty() && StartsWithI(files[i], WithSlash(other))) { why = L"bk.err_wim_in_source"; break; }
                if (!why.empty()) { Info(L::F(why, folders[i], files[i]), MB_ICONWARNING); return; }
            }
            // each WIM file once: new, or images added to it
            std::vector<wstring> unique;
            std::vector<long long> counts;
            for (const wstring& f : files)
            {
                size_t k = 0;
                while (k < unique.size() && !EqualsI(unique[k], f)) k++;
                if (k == unique.size()) { unique.push_back(f); counts.push_back(0); }
                counts[k]++;
            }
            wstring lines;
            bool anyExisting = false;
            for (size_t k = 0; k < unique.size(); k++)
            {
                bool exists = FileExists(unique[k]);
                anyExisting = anyExisting || exists;
                lines += L::F(exists ? L"mw.confirm_add" : L"mw.confirm_new", unique[k], counts[k]) + L"\r\n";
            }
            int ci = std::max(0, ComboIndex(cboComp));
            wstring comp = ComboItem(cboComp, ci) + (anyExisting ? L"\r\n" + L::T(L"mw.confirm_existing") : wstring());
            if (Checked(chkSnapshot)) comp += L"\r\n" + L::T(L"bk.confirm_snapshot");
            if (Checked(chkCheck)) comp += L"\r\n" + L::T(L"bk.confirm_check");
            // no folders come in while it asks (the others wait)
            struct Asking { bool& b; ~Asking() { b = false; } } guard{ asking };
            asking = true;
            // the target on a slow link (USB 2.0 while the PC has USB 3.0, or a
            // network link of 100 Mbit/s or slower): a warning, then on as before.
            // Checked here, not in the background: [Start] may come by itself
            // a second after the folders, and a later message would cross it.
            if (!EqualsI(target, linkChecked))
            {
                linkChecked = target;
                HCURSOR wait = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
                wstring warn = SlowLinkText(target);
                SetCursor(wait);
                if (!warn.empty()) Info(warn, MB_ICONWARNING);
            }
            if (confirm && !YesNo(L::F(L"mw.confirm", TrimEnd(lines), comp), MB_ICONQUESTION)) return;

            // wimlib for the helper (downloaded when missing)
            wstring exeDir = ExeDir(), lib = Combine(exeDir, L"lib");
            HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            libs::Migrate(exeDir);
            wstring err = libs::EnsureWimlib(exeDir);
            SetCursor(old);
            if (!err.empty()) { Info(L::F(L"bk.no_wimlib", err, lib), MB_ICONWARNING); return; }

            // the elevated helper does not see the network drives of this session
            Job job;
            job[L"mode"] = L"capture";
            job[L"source_count"] = Num((long long)folders.size());
            for (size_t i = 0; i < folders.size(); i++)
            {
                wstring k = Num((long long)i + 1);
                job[L"s" + k] = SessionFreePath(folders[i]);
                job[L"w" + k] = SessionFreePath(files[i]);
                job[L"n" + k] = NameOf(folders[i]);
            }
            job[L"compression"] = Num(CompTypes[std::min(2, ci)]);
            job[L"threads"] = Num(NumberValue(numThreads));
            job[L"append"] = L"1";   // an existing file gets the folders as new images
            job[L"verify"] = Checked(chkVerify) ? L"1" : L"0";
            job[L"check"] = Checked(chkCheck) ? L"1" : L"0";
            job[L"snapshot"] = snapshot ? L"1" : L"0";
            job[L"tools"] = SessionFreePath(lib);
            job[L"root"] = SessionFreePath(exeDir);
            workDir = NewWorkDir();
            try { process = StartElevated(job, workDir, d); }
            catch (const HelperError& e) { Info(L::F(L"mw.failed", wstring(L"-"), e.msg, workDir), MB_ICONERROR); return; }
            if (!process) return;   // UAC cancelled
            gather::Stop();
            written = unique;
            running = true;
            started = GetTickCount64();
            busy();
            Show(bar, true);
            ProgressMarquee(bar, true);
            SetText(lblState, L::T(L"bk.phase_start"));
            ui::SetTimer(d, 1, 250, poll);
        };

        OnClick(rbOne, [&] { setMode(false); });
        OnClick(rbEach, [&] { setMode(true); });
        OnChange(txtTarget, fillList);
        OnClick(btnTarget, [&]
        {
            wstring cur = FullPath(Trim(GetText(txtTarget)));
            wstring picked;
            if (each)
            {
                if (PickFolder(L::T(L"mw.select_folder"), cur, picked)) SetText(txtTarget, picked);
            }
            else if (SaveFile({ { L"WIM (*.wim)", L"*.wim" } }, cur.empty() ? wstring() : DirName(cur),
                         cur.empty() ? wstring() : FileName(cur), false, picked))
                SetText(txtTarget, picked);
        });
        OnClick(btnAdd, [&]
        {
            wstring picked;
            if (!PickFolder(L::T(L"bk.select_source"), folders.empty() ? wstring() : DirName(folders[0]), picked)) return;
            addFolders({ picked });
            updateTarget();
        });
        OnClick(btnRemove, [&]
        {
            std::vector<int> sel;
            for (int i = ListView_GetNextItem(list, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(list, i, LVNI_SELECTED)) sel.push_back(i);
            for (size_t n = sel.size(); n-- > 0;)
                if (sel[n] < (int)folders.size()) folders.erase(folders.begin() + sel[n]);
            updateTarget();
        });
        OnClick(btnStart, [&] { start(true); });
        OnClick(btnClose, [&] { if (running) cancel(); else EndModal(d, 0); });
        OnClose(d, [&] { if (running) cancel(); else EndModal(d, 0); return false; });
        OnKeys(d, nullptr, [&] { if (!running) EndModal(d, 0); });

        // the folders of the other processes (the other selected folders)
        gather::Receive(L"MakeWim." + mode, [&](const std::vector<wstring>& more)
        {
            if (running || asking) return false;
            addFolders(more);
            updateTarget();
            lastFolder = GetTickCount64();
            return true;
        });
        if (mode == L"each") { SetChecked(rbOne, false); SetChecked(rbEach, true); setMode(true); }
        updateTarget();
        if (autoStart)
        {
            SetText(lblState, L::T(L"mw.gathering"));
            ui::SetTimer(d, 2, 200, [&] { if (GetTickCount64() - lastFolder >= 1000) start(false); });
        }
        Relayout(d);
        SetFocus(btnStart);
        RunModal(d);
        gather::Stop();
        if (process)
        {
            // only when the window was closed in another way: the job still ends
            WaitForSingleObject(process, INFINITE);
            CloseHandle(process);
        }
        return 0;
    }
}
