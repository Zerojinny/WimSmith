// ============================================================
//  7z_wimlib - tab_splitjoin.cpp
//  The split / join tab (see tab_splitjoin.h).
// ============================================================
#include "tab_splitjoin.h"
#include "helper.h"
#include "lang.h"
#include <algorithm>
#include <commctrl.h>

namespace sevenwim
{
    using namespace ui;

    // <folder of the WIM>\<name of the WIM>.swm
    static wstring DefaultSwm(const wstring& wim)
    {
        wstring full = FullPath(wim);
        return full.empty() ? wstring() : Combine(DirName(full), FileStem(full) + L".swm");
    }

    SplitJoinTab::SplitJoinTab(App& a, HWND p) : app(a), page(p), splitGrid(nullptr), joinGrid(nullptr)
    {
        split = Group(page, L::T(L"sj.group_split"));
        splitGrid.parent = split;
        txtSrc = Edit(split);
        btnSrc = Button(split, L::T(L"qbt.btn_browse"));
        splitGrid.Add(L::T(L"ex.row_source"), { Field(txtSrc, 420), Btn(btnSrc, 100) });
        txtSwm = Edit(split);
        btnSwm = Button(split, L::T(L"qbt.btn_browse"));
        splitGrid.Add(L::T(L"sj.row_swm"), { Field(txtSwm, 420), Btn(btnSwm, 100) });
        numPart = Number(split, 10, 1048576, 4000);
        NumberStep(numPart, 100);
        lblMb = Label(split, L::T(L"sj.part_hint"), FontUI, color::Dim);
        btnSplit = Button(split, L::T(L"sj.btn_split"));
        Primary(btnSplit, false);
        Item part = Field(numPart, 90);
        part.right = 4;
        splitGrid.Add(L::T(L"sj.row_part"), { part, Txt(lblMb, 0, 12) });
        chkSplitCheck = Check(split, L::T(L"sj.check_split"));
        splitGrid.Add(L::T(L"bk.row_check"), { Box(chkSplitCheck) });
        splitGrid.Add(L"", { Btn(btnSplit, 140) });

        join = Group(page, L::T(L"sj.group_join"));
        joinGrid.parent = join;
        txtFirst = Edit(join);
        btnFirst = Button(join, L::T(L"qbt.btn_browse"));
        lblParts = Label(join, L"", FontUI, color::Dim);
        joinGrid.Add(L::T(L"sj.row_first"), { Field(txtFirst, 420), Btn(btnFirst, 100), Txt(lblParts) });
        txtWim = Edit(join);
        btnWim = Button(join, L::T(L"qbt.btn_browse"));
        joinGrid.Add(L::T(L"sj.row_join_wim"), { Field(txtWim, 420), Btn(btnWim, 100) });
        chkJoinCheck = Check(join, L::T(L"sj.check_join"));
        joinGrid.Add(L::T(L"bk.row_check"), { Box(chkJoinCheck) });
        btnJoin = Button(join, L::T(L"sj.btn_join"));
        Primary(btnJoin, false);
        joinGrid.Add(L"", { Btn(btnJoin, 140) });

        SetLayout(page, [this](int w, int h) { Layout(w, h); });
        SetLayout(split, [this](int w, int) { splitGrid.Place(S(6), GroupTop(), w - S(12)); });
        SetLayout(join, [this](int w, int) { joinGrid.Place(S(6), GroupTop(), w - S(12)); });

        OnClick(btnSrc, [this] { ChooseSource(); });
        OnClick(btnSwm, [this] { ChooseSwm(); });
        OnClick(btnSplit, [this] { StartSplit(); });
        OnClick(btnFirst, [this] { ChooseParts(); });
        OnClick(btnWim, [this] { ChooseJoinTarget(); });
        OnClick(btnJoin, [this] { StartJoin(); });
        // the parts are counted when the field is left (C#: Leave)
        SetWindowSubclass(txtFirst, [](HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR self) -> LRESULT
        {
            if (m == WM_KILLFOCUS) ((SplitJoinTab*)self)->ShowParts();
            return DefSubclassProc(h, m, wp, lp);
        }, 2, (DWORD_PTR)this);
        app.busyHandlers.push_back([this](bool busy) { SetBusy(busy); });
    }

    void SplitJoinTab::Layout(int w, int h)
    {
        // two halves, 4 px apart
        int half = (h - S(4)) / 2;
        Move(split, 0, 0, w, half);
        Move(join, 0, half + S(4), w, h - half - S(4));
    }

    void SplitJoinTab::Refresh()
    {
        wstring def = app.DefaultWim();
        if (Trim(GetText(txtSrc)).empty() && FileExists(def)) { SetText(txtSrc, def); SetText(txtSwm, DefaultSwm(def)); }
    }

    void SplitJoinTab::ChooseSource()
    {
        wstring cur = Trim(GetText(txtSrc)), file;
        if (!OpenFile({ { L"WIM (*.wim)", L"*.wim" }, { L"*.*", L"*.*" } }, DirName(FullPath(cur)), file)) return;
        SetText(txtSrc, file);
        SetText(txtSwm, DefaultSwm(file));
    }

    void SplitJoinTab::ChooseSwm()
    {
        wstring cur = Trim(GetText(txtSwm));
        if (cur.empty()) cur = DefaultSwm(Trim(GetText(txtSrc)));
        wstring full = FullPath(cur), file;
        if (!SaveFile({ { L"SWM (*.swm)", L"*.swm" } }, full.empty() ? wstring() : DirName(full),
                full.empty() ? wstring(L"install.swm") : FileName(full), false, file)) return;
        SetText(txtSwm, file);
    }

    void SplitJoinTab::ChooseParts()
    {
        wstring cur = Trim(GetText(txtFirst)), file;
        if (!OpenFile({ { L"SWM (*.swm)", L"*.swm" }, { L"*.*", L"*.*" } }, DirName(FullPath(cur)), file)) return;
        std::vector<wstring> parts = SwmParts(file);
        SetText(txtFirst, parts.empty() ? file : parts[0]);
        SetText(txtWim, L"");
        ShowParts();
    }

    // "<n> parts found" next to the first part; the default target
    void SplitJoinTab::ShowParts()
    {
        wstring first = Trim(GetText(txtFirst));
        std::vector<wstring> parts = SwmParts(first);
        SetText(lblParts, first.empty() ? wstring() : parts.empty() ? L::T(L"sj.parts_none") : L::F(L"sj.parts_found", (long long)parts.size()));
        Relayout(join);
        if (!parts.empty() && Trim(GetText(txtWim)).empty())
        {
            wstring dir = DirName(parts[0]), stem = FileStem(parts[0]);
            wstring wim = Combine(dir, stem + L".wim");
            if (FileExists(wim)) wim = Combine(dir, stem + L"_joined.wim");
            SetText(txtWim, wim);
        }
    }

    void SplitJoinTab::ChooseJoinTarget()
    {
        wstring cur = Trim(GetText(txtWim));
        wstring full = FullPath(cur.empty() ? Trim(GetText(txtFirst)) : cur), file;
        if (!SaveFile({ { L"WIM (*.wim)", L"*.wim" } }, full.empty() ? wstring() : DirName(full),
                cur.empty() ? wstring(L"joined.wim") : FileName(FullPath(cur)), false, file)) return;
        SetText(txtWim, file);
    }

    void SplitJoinTab::SetBusy(bool busy)
    {
        for (HWND c : { btnSplit, btnJoin, txtSrc, txtSwm, txtFirst, txtWim, chkSplitCheck, chkJoinCheck }) Enable(c, !busy);
    }

    void SplitJoinTab::StartSplit()
    {
        if (app.Busy()) return;
        wstring source = Trim(GetText(txtSrc)), swm = Trim(GetText(txtSwm));
        wstring why = SplitProblem(source, swm);
        if (!why.empty()) { Info(L::F(why, source.empty() ? wstring(L"-") : source, swm), MB_ICONWARNING); return; }
        if (!app.LoadWimlib()) return;
        // wimlib cannot split solid resources (ESD, LZMS solid)
        wl::WimFileInfo f;
        int rc = wl::ReadInfo(source, f);
        if (rc != 0) { Info(L::F(L"bk.read_failed", source, wl::ErrorText(rc)), MB_ICONWARNING); return; }
        if (f.Solid()) { Info(L::F(L"sj.err_solid", source), MB_ICONWARNING); return; }
        if (f.TotalParts > 1) { Info(L::F(L"sj.err_already_split", source), MB_ICONWARNING); return; }
        long long partMb = NumberValue(numPart);
        long long size = f.FileBytes > 0 ? f.FileBytes : f.TotalBytes;
        long long estimate = std::max(1LL, (size + partMb * MiB - 1) / (partMb * MiB));
        std::vector<wstring> old = SwmParts(swm);
        wstring what = !old.empty() ? L"\r\n\r\n" + L::F(L"sj.confirm_old_parts", (long long)old.size(), DirName(FullPath(swm))) : wstring();
        if (Checked(chkSplitCheck)) what += L"\r\n\r\n" + L::T(L"sj.confirm_check_split");
        if (!YesNo(L::F(L"sj.confirm_split", source, swm, App::Group(partMb), estimate, what), old.empty() ? MB_ICONQUESTION : MB_ICONWARNING))
            return;

        Job job;
        job[L"mode"] = L"split";
        job[L"source"] = source;
        job[L"swm"] = swm;
        job[L"part_mb"] = Num(partMb);
        job[L"check"] = Checked(chkSplitCheck) ? L"1" : L"0";
        app.job = App::JobInfo();
        app.job.wim = swm;
        app.onSuccess = [this](const wstring& elapsed)
        {
            wstring w = app.job.wim, list;
            std::vector<wstring> parts = SwmParts(w);
            for (const wstring& part : parts)
            {
                long long len = FileSize(part);
                list += (list.empty() ? L"" : L"\r\n") + (L"  " + FileName(part) + L"  ("
                    + (len >= 0 ? App::Group((len + MiB / 2) / MiB) + L" MB" : wstring(L"?")) + L")");
            }
            // the join box shows the new parts
            SetText(txtFirst, w);
            ShowParts();
            return L::F(L"sj.split_done", (long long)parts.size(), DirName(FullPath(w)), list) + L"\r\n\r\n" + elapsed;
        };
        app.onFailure = nullptr;
        app.Log(L::F(L"sj.log_split", source, swm, App::Group(partMb)));
        app.StartHelper(job);
    }

    void SplitJoinTab::StartJoin()
    {
        if (app.Busy()) return;
        ShowParts();
        wstring first = Trim(GetText(txtFirst)), wim = Trim(GetText(txtWim));
        std::vector<wstring> parts = SwmParts(first);
        wstring why = JoinProblem(parts, wim);
        if (!why.empty()) { Info(L::F(why, first.empty() ? wstring(L"-") : first, wim), MB_ICONWARNING); return; }
        if (!app.LoadWimlib()) return;
        // the first part tells how many parts there are
        wl::WimFileInfo f;
        int rc = wl::ReadInfo(parts[0], f);
        if (rc != 0) { Info(L::F(L"bk.read_failed", parts[0], wl::ErrorText(rc)), MB_ICONWARNING); return; }
        if (f.TotalParts <= 1) { Info(L::F(L"sj.err_not_split", parts[0]), MB_ICONWARNING); return; }
        if (f.PartNumber != 1) { Info(L::F(L"sj.err_not_first", parts[0], f.PartNumber), MB_ICONWARNING); return; }
        if ((int)parts.size() != f.TotalParts) { Info(L::F(L"sj.err_part_count", parts[0], f.TotalParts, (long long)parts.size()), MB_ICONWARNING); return; }
        long long total = 0;
        for (const wstring& part : parts) total += std::max(0LL, FileSize(part));
        wstring what = FileExists(wim) ? L::T(L"bk.confirm_replace") : L::T(L"bk.confirm_new");
        if (Checked(chkJoinCheck)) what += L"\r\n" + L::T(L"sj.confirm_check_join");
        if (!YesNo(L::F(L"sj.confirm_join", parts[0], (long long)parts.size(), App::Gb(total), wim, what), MB_ICONQUESTION)) return;

        wstring list;
        for (const wstring& part : parts) list += (list.empty() ? L"" : L"|") + part;
        Job job;
        job[L"mode"] = L"join";
        job[L"parts"] = list;
        job[L"check"] = Checked(chkJoinCheck) ? L"1" : L"0";
        job[L"wim"] = wim;
        app.job = App::JobInfo();
        app.job.wim = wim;
        app.onSuccess = [this](const wstring& elapsed)
        {
            // the other tabs show the joined file when they showed it before
            app.WimWritten(app.job.wim, false);
            return L::F(L"sj.join_done", app.job.wim) + L"\r\n\r\n" + elapsed;
        };
        app.onFailure = nullptr;
        app.Log(L::F(L"sj.log_join", (long long)parts.size(), parts[0], wim));
        app.StartHelper(job);
    }
}
