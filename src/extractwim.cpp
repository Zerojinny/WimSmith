// ============================================================
//  WimSmith - extractwim.cpp
//  "Extract" / "Extract each" of the right-click menu of WIM files,
//  without the main window (extractwim.h).
// ============================================================
#include "extractwim.h"
#include "gather.h"
#include "helper.h"
#include "lang.h"
#include "libs.h"
#include "settings.h"
#include "ui.h"
#include "wim.h"
#include <atomic>
#include <memory>
#include <thread>

namespace wimsmith
{
    using namespace ui;

    bool ExtractMode(const wstring& s) { return s == L"one" || s == L"each"; }

    bool ExtractForward(const std::vector<wstring>& files, const wstring& mode)
    {
        return gather::Forward(L"Extract." + mode, files);
    }

    // ---------- names ----------

    // a name Windows allows for a folder ("" stays "")
    static wstring SafeName(const wstring& s)
    {
        wstring r;
        for (wchar_t c : s) r += (c < 32 || wcschr(L"<>:\"/\\|?*", c)) ? L'_' : c;
        while (!r.empty() && (r.back() == L' ' || r.back() == L'.')) r.pop_back();
        return r;
    }

    static bool Exists(const wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

    // <dir>\<name>, or "<name> (2)", ... when that exists or is taken by another file of this job
    static wstring NewFolder(const wstring& dir, const wstring& name, const std::vector<wstring>& taken)
    {
        wstring base = Combine(dir, name.empty() ? wstring(L"WIM") : name), p = base;
        auto isTaken = [&](const wstring& x) { for (const wstring& t : taken) if (EqualsI(t, x)) return true; return false; };
        for (int n = 2; Exists(p) || isTaken(p); n++) p = base + L" (" + Num(n) + L")";
        return p;
    }

    // ---------- extracting (in a thread) ----------

    struct Task { wstring file, folder; };

    struct Context
    {
        std::atomic<bool>* stop;
        HWND bar, label;
        wstring text;
        int lastPct = -2;
    };

    static void Show(Context* c, int pct)
    {
        if (pct == c->lastPct) return;
        c->lastPct = pct;
        HWND bar = c->bar, label = c->label;
        wstring text = c->text + (pct >= 0 ? L"  " + Num(pct) + L"%" : wstring());
        Post(GetParent(bar), [bar, label, text, pct]
        {
            if (pct >= 0) { ProgressMarquee(bar, false); ProgressValue(bar, pct); }
            else ProgressMarquee(bar, true);
            SetText(label, text);
        });
    }

    static int __cdecl OnProgress(int msg, void* info, void* ctx)
    {
        Context* c = (Context*)ctx;
        if (c->stop->load()) return wl::StatusAbort;
        if (msg == wl::MsgExtractStreams && info)
        {
            // struct wimlib_progress_info_extract: total_bytes (40), completed_bytes (48)
            uint64_t total = *(const uint64_t*)((const char*)info + 40), done = *(const uint64_t*)((const char*)info + 48);
            Show(c, total > 0 ? (int)(done * 100 / total) : 0);
        }
        return wl::StatusContinue;
    }

    // every image of every file into its folder; done(rc, error) on the GUI thread
    static void Extract(const std::vector<Task>& tasks, std::shared_ptr<std::atomic<bool>> stop, HWND d, HWND bar, HWND label,
        std::function<void(int, const wstring&)> done)
    {
        std::thread([tasks, stop, d, bar, label, done]
        {
            int rc = 0;
            wstring err, messages = wl::BeginMessages();
            for (size_t i = 0; i < tasks.size() && rc == 0; i++)
            {
                const Task& t = tasks[i];
                wstring head = tasks.size() > 1 ? L::F(L"xw.file_of", (long long)i + 1, (long long)tasks.size()) + L" " : wstring();
                Context c = { stop.get(), bar, label, head + FileName(t.file) };
                Show(&c, -1);
                wl::Wim w;
                int total = 1, found = 1;
                rc = wl::OpenRead(t.file, 0, nullptr, w, total, found);
                if (rc == wl::ErrPartsMissing) { err = L::F(L"xw.parts_missing", t.file, (long long)found, (long long)total); break; }
                if (rc == 0)
                {
                    wl::WimInfo info;
                    int count = info.Read(w.p) ? info.ImageCount() : 1;
                    wl::wimlib_register_progress_function(w.p, OnProgress, &c);
                    for (int im = 1; im <= count && rc == 0; im++)
                    {
                        // several images: a subfolder "<index> <name>" each
                        wstring target = t.folder;
                        if (count > 1)
                        {
                            wstring name = SafeName(wl::Property(w.p, im, L"NAME"));
                            target = Combine(t.folder, name.empty() ? Num(im) : Num(im) + L" " + name);
                            c.text = head + FileName(t.file) + L"  " + L::F(L"xw.image_of", (long long)im, (long long)count);
                            c.lastPct = -2;
                            Show(&c, -1);
                        }
                        try { CreateDirs(target); }
                        catch (const HelperError& e) { err = e.msg; rc = -1; break; }
                        // no ACLs: no administrator rights needed; names Windows does not allow are changed
                        rc = wl::wimlib_extract_image(w.p, im, target.c_str(), wl::ExtractNoAcls | wl::ExtractReplaceInvalidNames);
                        if (stop->load()) rc = wl::ErrAborted;
                    }
                }
                if (rc != 0 && rc != wl::ErrAborted && err.empty())
                {
                    wstring more = wl::Messages(messages);
                    err = t.file + L"\r\n\r\n" + (more.empty() ? wstring() : more + L"\r\n\r\n") + wl::ErrorText(rc);
                }
            }
            Post(d, [done, rc, err] { done(rc, err); });
        }).detach();
    }

    // ---------- the window ----------

    int RunExtract(const std::vector<wstring>& given, const wstring& mode)
    {
        std::vector<wstring> files;
        // any part of a split WIM: its first part, once
        auto addFiles = [&files](const std::vector<wstring>& list)
        {
            for (const wstring& f : list)
            {
                wstring full = FullPath(Trim(f));
                if (full.empty() || !FileExists(full)) continue;
                if (EqualsI(FileExt(full), L".swm"))
                {
                    std::vector<wstring> parts = SwmParts(full);
                    if (!parts.empty()) full = parts[0];
                }
                bool known = false;
                for (const wstring& k : files) if (EqualsI(k, full)) known = true;
                if (!known) files.push_back(full);
            }
        };
        addFiles(given);

        HWND d = Dialog(L::T(L"xw.title"), 560, 150);
        SetWindowLongPtrW(d, GWL_EXSTYLE, GetWindowLongPtrW(d, GWL_EXSTYLE) | WS_EX_APPWINDOW);
        HWND label = Label(d, L::T(L"xw.gathering"));
        HWND bar = ui::Progress(d);
        HWND btnClose = Button(d, L::T(L"bk.btn_cancel"));
        ProgressMarquee(bar, true);
        SetLayout(d, [=](int cw, int ch)
        {
            Move(label, S(14), S(14), cw - S(28), S(40));
            Move(bar, S(14), S(62), cw - S(28), S(6));
            Move(btnClose, cw - S(14) - S(96), ch - S(14) - S(28), S(96), S(28));
        });

        bool started = false, running = false;
        ULONGLONG lastFile = GetTickCount64(), t0 = 0;
        std::vector<wstring> folders;
        auto stop = std::make_shared<std::atomic<bool>>(false);
        gather::Receive(L"Extract." + mode, [&](const std::vector<wstring>& more)
        {
            if (started) return false;
            addFiles(more);
            lastFile = GetTickCount64();
            return true;
        });

        auto finish = [&](int rc, const wstring& err)
        {
            running = false;
            ProgressMarquee(bar, false);
            ProgressValue(bar, rc == 0 ? 100 : 0);
            SetText(btnClose, L::T(L"mw.btn_close"));
            Enable(btnClose, true);
            wstring list;
            for (const wstring& f : folders) list += f + L"\r\n";
            if (rc == 0)
            {
                ULONGLONG s = (GetTickCount64() - t0) / 1000;
                SetText(label, L::T(L"mw.state_done"));
                Info(L::F(L"xw.done", TrimEnd(list), (long long)(s / 60), (long long)(s % 60)));
                EndModal(d, 1);
                return;
            }
            if (rc == wl::ErrAborted)
            {
                SetText(label, L::T(L"bk.state_cancelled"));
                Info(L::F(L"xw.cancelled", TrimEnd(list)));
                return;
            }
            SetText(label, L::F(L"mw.state_failed", (long long)rc));
            Info(L::F(L"xw.failed", err), MB_ICONERROR);
        };

        auto start = [&]
        {
            ui::KillTimer(d, 1);
            started = true;
            gather::Stop();
            if (files.empty()) { Info(L::T(L"xw.no_files"), MB_ICONWARNING); EndModal(d, 0); return; }
            // wimlib in this process (downloaded when missing)
            wstring exeDir = ExeDir(), lib = Combine(exeDir, L"lib");
            libs::Migrate(exeDir);
            wstring err = libs::EnsureWimlib(exeDir);
            if (err.empty()) err = wl::Load(lib, false);
            if (!err.empty()) { Info(L::F(L"bk.no_wimlib", err, lib), MB_ICONWARNING); EndModal(d, 0); return; }
            // the new folders, next to the files
            std::vector<Task> tasks;
            if (mode == L"one")
            {
                folders.push_back(NewFolder(DirName(files[0]), SafeName(FileStem(files[0])), folders));
                for (const wstring& f : files) tasks.push_back({ f, folders[0] });
            }
            else
                for (const wstring& f : files)
                {
                    folders.push_back(NewFolder(DirName(f), SafeName(FileStem(f)), folders));
                    tasks.push_back({ f, folders.back() });
                }
            running = true;
            t0 = GetTickCount64();
            Extract(tasks, stop, d, bar, label, finish);
        };

        auto cancel = [&]
        {
            if (!running) { if (!started) { started = true; gather::Stop(); } EndModal(d, 0); return; }
            stop->store(true);
            Enable(btnClose, false);
            SetText(label, L::T(L"bk.log_cancelling"));
        };
        OnClick(btnClose, cancel);
        OnClose(d, [&] { cancel(); return false; });
        OnKeys(d, nullptr, [&] { if (!running) cancel(); });
        // start once no file came in for a second (the others of the selection)
        ui::SetTimer(d, 1, 200, [&] { if (!started && GetTickCount64() - lastFile >= 1000) start(); });
        Relayout(d);
        RunModal(d);
        gather::Stop();
        // the window closed while it ran (not by [Cancel]): stop wimlib and wait a moment for it
        if (running)
        {
            stop->store(true);
            for (int n = 0; n < 100 && running; n++)
            {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
                Sleep(50);
            }
        }
        return 0;
    }
}
