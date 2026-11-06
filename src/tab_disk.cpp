// ============================================================
//  7z_wimlib - tab_disk.cpp
//  The disk tab (see tab_disk.h).
// ============================================================
#include "tab_disk.h"
#include "helper.h"
#include "lang.h"
#include "settings.h"
#include "tab_restore.h"
#include <algorithm>
#include <climits>
#include <thread>

namespace dvt
{
    using namespace ui;

    static const wchar_t* const DataFs[] = { L"NTFS", L"exFAT", L"FAT32" };

    DiskTab::DiskTab(App& a, HWND p) : app(a), page(p), grid(p)
    {
        cboDisk = Combo(page);
        btnDiskRefresh = Button(page, L::T(L"adk.btn_refresh"));
        grid.Add(L::T(L"dk.row_disk"), { Field(cboDisk, 560), Btn(btnDiskRefresh, 90) });
        rbGpt = Radio(page, L::T(L"dk.style_gpt"), 1);
        rbMbr = Radio(page, L::T(L"dk.style_mbr"), 1);
        SetChecked(rbGpt, true);
        grid.Add(L::T(L"dk.row_style"), { Box(rbGpt), Box(rbMbr) });
        rbLayWin = Radio(page, L::T(L"dk.layout_windows"), 2);
        rbLayWinData = Radio(page, L::T(L"dk.layout_windows_data"), 2);
        rbLayData = Radio(page, L::T(L"dk.layout_data"), 2);
        SetChecked(rbLayWin, true);
        grid.Add(L::T(L"dk.row_layout"), { Box(rbLayWin), Box(rbLayWinData), Box(rbLayData) });
        lblSysKind = Label(page, L"");
        numSys = Number(page, 50, 4096, 260);
        NumberStep(numSys, 10);
        lblSysMb = Label(page, L"MB");
        chkMsr = Check(page, L::F(L"dk.msr_make", DiskPlan::MsrMb));
        SetChecked(chkMsr, true);
        Item sysKind = Txt(lblSysKind, 0, 8);
        sysKind.w = 200;                      // the C# MinimumSize: the two kinds line up
        Item sysNum = Field(numSys, 80);
        sysNum.right = 4;
        grid.Add(L::T(L"dk.row_sys"), { sysKind, sysNum, Txt(lblSysMb, 0, 16), Box(chkMsr) });
        chkRec = Check(page, L::T(L"dk.recovery_make"));
        numRec = Number(page, 300, 65536, 1024);
        NumberStep(numRec, 10);
        lblRecMb = Label(page, L"MB");
        Item recNum = Field(numRec, 80);
        recNum.right = 4;
        grid.Add(L::T(L"dk.row_recovery"), { Box(chkRec), recNum, Txt(lblRecMb, 0, 8) });
        txtDiskLabel = Edit(page);
        SetText(txtDiskLabel, L"Windows");
        grid.Add(L::T(L"dk.row_win_label"), { Field(txtDiskLabel, 200) });
        // data partition: share of Windows (the data partition gets the rest), file system, label
        lblPct = Label(page, L::T(L"dk.win_pct"));
        numWinPct = Number(page, 5, 95, 50);
        NumberStep(numWinPct, 5);
        lblPctSign = Label(page, L"%");
        cboDataFs = Combo(page);
        for (const wchar_t* fs : DataFs) ComboAdd(cboDataFs, fs);
        ComboSelect(cboDataFs, 0);
        lblLbl = Label(page, L::T(L"dk.label"));
        txtDataLabel = Edit(page);
        SetText(txtDataLabel, L"Data");
        Item pct = Field(numWinPct, 56);
        pct.right = 4;
        grid.Add(L::T(L"dk.row_data"), { Txt(lblPct, 0, 4), pct, Txt(lblPctSign, 0, 16), Field(cboDataFs, 80), Txt(lblLbl, 8, 4), Field(txtDataLabel, 140) });
        lblPlan = Label(page, L"");
        grid.Add(L::T(L"dk.row_plan"), {});
        btnGo = Button(page, L::T(L"dk.btn_go"));
        Primary(btnGo, true);
        hint = Label(page, L::T(L"dk.hint"), FontUI, color::Dim);
        grid.Add(L"", { Btn(btnGo, 220) });
        SetLayout(page, [this](int w, int h) { Layout(w, h); });

        LoadSettings();
        OnClick(btnDiskRefresh, [this] { LoadDisks(); });
        OnClick(btnGo, [this] { StartDisk(); });
        OnChange(rbGpt, [this] { if (Checked(rbGpt) || Checked(rbMbr)) { SetNumber(numSys, DiskPlan::DefaultSysMb(Checked(rbGpt))); UpdateUi(); } });
        for (HWND rb : { rbLayWin, rbLayWinData, rbLayData }) OnChange(rb, [this, rb] { if (Checked(rb)) UpdateUi(); });
        for (HWND h : { numSys, numRec, numWinPct, chkRec, chkMsr, cboDataFs, cboDisk, txtDiskLabel, txtDataLabel })
            OnChange(h, [this] { UpdateUi(); });
        // the disks are read the first time the tab is shown
        OnChange(app.win.tabs, [this] { if (app.win.PageShown(page) && !disksRead && !disksLoading) LoadDisks(); });
        app.busyHandlers.push_back([this](bool busy) { SetBusy(busy); });
        settingsSaved = SettingsText(Settings());
        loaded = true;
        FillDisks();
    }

    void DiskTab::Layout(int w, int)
    {
        // rows of 34 px; the plan gets 112 px, then the button and the hint
        int y = 0;
        for (size_t i = 0; i < grid.rows.size(); i++)
        {
            const Grid::Row& r = grid.rows[i];
            bool plan = i == 7;
            Move(r.label, 0, y, S(grid.labelWidth), S(grid.rowHeight));
            int x = S(grid.labelWidth);
            if (plan) Move(lblPlan, x, y + S(8), w - x, S(104));
            else Grid::PlaceRow(r.items, x, y, w - x);
            if (i == 8)
            {
                int hx = x + S(220) + S(8) + S(8);
                int hw = std::min(S(520), w - hx);
                Move(hint, hx, y + S(4), std::max(0, hw), TextHeightWrapped(FontUI, GetText(hint), hw) + S(2));
            }
            y += plan ? S(112) : S(grid.rowHeight);
        }
    }

    // ---------- settings (7z_wimlib.ini, disk.*) ----------
    // Every choice of the tab is kept, except the disk: a disk to clear is
    // always chosen by hand.

    std::vector<std::pair<wstring, wstring>> DiskTab::Settings() const
    {
        DiskPlan p = CurrentPlan();
        return {
            { L"disk.style", p.Gpt ? L"GPT" : L"MBR" },
            { L"disk.layout", p.Layout() },
            { L"disk.sys_mb", Num(p.SysMb) },
            { L"disk.msr", p.Msr ? L"1" : L"0" },
            { L"disk.recovery", p.Recovery ? L"1" : L"0" },
            { L"disk.recovery_mb", Num(p.RecoveryMb) },
            { L"disk.win_pct", Num(p.WindowsPercent) },
            { L"disk.win_label", Trim(GetText(txtDiskLabel)) },
            { L"disk.data_fs", p.DataFs },
            { L"disk.data_label", Trim(GetText(txtDataLabel)) },
        };
    }

    wstring DiskTab::SettingsText(const std::vector<std::pair<wstring, wstring>>& d)
    {
        wstring s;
        for (const auto& kv : d) s += kv.first + L"=" + kv.second + L"\n";
        return s;
    }

    static void LoadNumber(HWND n, const wchar_t* key)
    {
        long long v;
        if (TryParseLong(GetSetting(key, L""), v)) SetNumber(n, (int)std::max<long long>(INT_MIN, std::min<long long>(INT_MAX, v)));
    }

    // before the callbacks are connected: nothing is saved while loading
    void DiskTab::LoadSettings()
    {
        bool mbr = EqualsI(GetSetting(L"disk.style", L"GPT"), L"MBR");
        SetChecked(mbr ? rbMbr : rbGpt, true);
        SetNumber(numSys, DiskPlan::DefaultSysMb(!mbr));
        wstring layout = GetSetting(L"disk.layout", L"windows");
        SetChecked(layout == L"data" ? rbLayData : layout == L"windows_data" ? rbLayWinData : rbLayWin, true);
        LoadNumber(numSys, L"disk.sys_mb");
        SetChecked(chkMsr, GetSetting(L"disk.msr", L"1") != L"0");
        SetChecked(chkRec, GetSetting(L"disk.recovery", L"0") == L"1");
        LoadNumber(numRec, L"disk.recovery_mb");
        LoadNumber(numWinPct, L"disk.win_pct");
        SetText(txtDiskLabel, GetSetting(L"disk.win_label", L"Windows"));
        wstring fs = GetSetting(L"disk.data_fs", L"NTFS");
        int pick = 0;
        for (int i = 0; i < 3; i++) if (fs == DataFs[i]) pick = i;
        ComboSelect(cboDataFs, pick);
        SetText(txtDataLabel, GetSetting(L"disk.data_label", L"Data"));
    }

    // after a change: written only when a value differs from the file
    void DiskTab::SaveSettings()
    {
        if (!loaded) return;
        auto d = Settings();
        wstring text = SettingsText(d);
        if (text == settingsSaved) return;
        settingsSaved = text;
        SetSettings(d);
    }

    // ---------- disks ----------

    // Get-Disk takes a few seconds: read in the background
    void DiskTab::LoadDisks()
    {
        if (disksLoading) return;
        disksLoading = true;
        disksRead = true;
        Enable(btnDiskRefresh, false);
        items.clear();
        ComboClear(cboDisk);
        items.push_back(DiskItem());
        ComboAdd(cboDisk, L::T(L"dk.reading"));
        ComboSelect(cboDisk, 0);
        HWND target = app.win.hwnd;
        std::thread([this, target]
        {
            std::vector<DiskInfo> list;
            wstring err;
            bool ok = QueryDisks(list, err);
            Post(target, [this, list, err, ok]
            {
                disksLoading = false;
                disks = list;
                disksKnown = ok;
                if (!ok) app.Log(L::F(L"dk.read_failed", err));
                FillDisks();
                SetBusy(app.Busy());
            });
        }).detach();
    }

    void DiskTab::FillDisks()
    {
        const DiskInfo* old = SelectedDisk();
        int keep = old ? old->Number : -1;
        items.clear();
        ComboClear(cboDisk);
        int pick = -1;
        if (disksKnown)
            for (const DiskInfo& d : disks)
            {
                DiskItem it;
                it.has = true;
                it.disk = d;
                it.problem = DiskProblem(d, app.root);
                wstring letters = d.Letters;
                for (size_t p; (p = letters.find(L',')) != wstring::npos;) letters.replace(p, 1, L": ");
                wstring text = L::F(L"dk.disk_line", d.Number, d.Name.empty() ? wstring(L"-") : d.Name, App::Gb(d.Size),
                    d.Style == L"RAW" ? L::T(L"dk.style_raw") : d.Style, d.Bus,
                    d.Letters.empty() ? L::T(L"dk.no_volumes") : letters + L":");
                if (!it.problem.empty()) text += L"  " + L::F(L"dk.not_usable", L::T(it.problem + L"_short"));
                items.push_back(it);
                ComboAdd(cboDisk, text);
                if (d.Number == keep) pick = (int)items.size() - 1;
            }
        if (items.empty())
        {
            items.push_back(DiskItem());
            ComboAdd(cboDisk, L::T(!disksKnown ? L"dk.unknown" : L"dk.none"));
        }
        else if (pick < 0)
        {
            // nothing is chosen for the user: a disk to clear is always picked by hand
            items.insert(items.begin(), DiskItem());
            SendMessageW(cboDisk, CB_INSERTSTRING, 0, (LPARAM)L::T(L"dk.choose").c_str());
            pick = 0;
        }
        ComboSelect(cboDisk, std::max(0, pick));
        UpdateUi();
    }

    void DiskTab::SetBusy(bool busy)
    {
        for (HWND c : { cboDisk, rbGpt, rbMbr, rbLayWin, rbLayWinData, rbLayData, btnGo }) Enable(c, !busy);
        Enable(btnDiskRefresh, !busy && !disksLoading);
        if (busy)
            for (HWND c : { numSys, chkMsr, chkRec, numRec, txtDiskLabel, numWinPct, cboDataFs, txtDataLabel }) Enable(c, false);
        else UpdateUi();
    }

    DiskPlan DiskTab::CurrentPlan() const
    {
        DiskPlan p;
        p.Gpt = Checked(rbGpt);
        p.Windows = !Checked(rbLayData);
        p.Data = !Checked(rbLayWin);
        p.SysMb = NumberValue(numSys);
        p.Msr = Checked(chkMsr);
        p.Recovery = Checked(chkRec);
        p.RecoveryMb = NumberValue(numRec);
        p.WindowsPercent = NumberValue(numWinPct);
        p.Label = GetText(txtDiskLabel);
        int fs = ComboIndex(cboDataFs);
        p.DataFs = fs >= 0 && fs < 3 ? DataFs[fs] : L"NTFS";
        p.DataLabel = GetText(txtDataLabel);
        return p;
    }

    const DiskInfo* DiskTab::SelectedDisk() const
    {
        int i = ComboIndex(cboDisk);
        return i >= 0 && i < (int)items.size() && items[i].has ? &items[i].disk : nullptr;
    }

    wstring DiskTab::MbText(long long bytes)
    {
        return bytes >= 10LL * 1073741824 ? App::Gb(bytes) : App::Group(bytes / MiB) + L" MB";
    }

    // enables the rows of the layout and shows the partitions that will be created
    void DiskTab::UpdateUi()
    {
        if (!loaded) return;
        DiskPlan p = CurrentPlan();
        bool gpt = p.Gpt, idle = !app.Busy();
        SetText(lblSysKind, L::T(gpt ? L"dk.sys_esp" : L"dk.sys_reserved"));
        Enable(numSys, idle && p.Windows);
        Enable(chkMsr, idle && p.Windows && gpt);
        Enable(chkRec, idle && p.Windows);
        Enable(numRec, idle && p.Windows && Checked(chkRec));
        Enable(txtDiskLabel, idle && p.Windows);
        Enable(numWinPct, idle && p.Windows && p.Data);
        Enable(cboDataFs, idle && p.Data);
        Enable(txtDataLabel, idle && p.Data);

        const DiskInfo* d = SelectedDisk();
        wstring rest = L::T(L"dk.plan_rest");
        wstring winSize = d && p.WindowsBytes(d->Size) > 0 ? MbText(p.WindowsBytes(d->Size)) : p.Data ? Num(p.WindowsPercent) + L"%" : rest;
        wstring dataSize = d && p.DataBytes(d->Size) > 0 ? MbText(p.DataBytes(d->Size)) : p.Windows ? Num(100 - p.WindowsPercent) + L"%" : rest;
        std::vector<wstring> lines;
        auto add = [&lines](const wstring& t) { lines.push_back(Num((long long)lines.size() + 1) + L". " + t); };
        if (p.Windows)
        {
            if (gpt)
            {
                add(L::F(L"dk.plan_esp", p.SysMb));
                if (p.HasMsr()) add(L::F(L"dk.plan_msr", DiskPlan::MsrMb));
            }
            else add(L::F(L"dk.plan_reserved", p.SysMb));
            add(L::F(L"dk.plan_windows", p.WindowsLabel(), winSize));
            if (p.Recovery) add(L::F(L"dk.plan_recovery", p.RecoveryMb));
        }
        if (p.Data) add(L::F(L"dk.plan_data", p.DataLabelClean(), dataSize, p.DataFs));
        wstring why = d ? PlanProblem(p, d->Size) : wstring();
        wstring text;
        for (size_t i = 0; i < lines.size(); i++) text += (i ? L"\r\n" : L"") + lines[i];
        // GPT without MSR: allowed, but Windows Setup and some tools expect it
        if (p.Windows && gpt && !p.Msr && why.empty()) text += L"\r\n" + L::T(L"dk.no_msr_note");
        if (!why.empty()) text += L"\r\n" + PlanText(why, *d, p);
        SetText(lblPlan, text);
        SetColor(lblPlan, why.empty() ? color::Text : color::Danger);
        SaveSettings();
    }

    // {0} disk size, {1} disk number, {2} Windows partition (or the only
    // data partition), {3} data partition
    wstring DiskTab::PlanText(const wstring& key, const DiskInfo& d, const DiskPlan& p) const
    {
        long long main = p.Windows ? p.WindowsBytes(d.Size) : p.DataBytes(d.Size);
        return L::F(key, App::Gb(d.Size), d.Number, MbText(std::max(0LL, main)), MbText(std::max(0LL, p.DataBytes(d.Size))));
    }

    void DiskTab::StartDisk()
    {
        if (app.Busy()) return;
        const DiskInfo* dp = SelectedDisk();
        if (!dp) { Info(L::T(L"dk.err_no_disk"), MB_ICONWARNING); return; }
        DiskInfo d = *dp;
        wstring why = DiskProblem(d, app.root);
        if (!why.empty()) { Info(L::F(why, d.Number), MB_ICONWARNING); return; }
        DiskPlan p = CurrentPlan();
        why = PlanProblem(p, d.Size);
        if (!why.empty()) { Info(PlanText(why, d, p), MB_ICONWARNING); return; }

        wstring letters = d.Letters;
        for (size_t q; (q = letters.find(L',')) != wstring::npos;) letters.replace(q, 1, L": ");
        wstring current = !d.Letters.empty() ? L::F(L"dk.confirm_volumes", letters + L":") : L::T(L"dk.confirm_no_volumes");
        wstring style = p.Gpt ? L"GPT" : L"MBR";
        if (!YesNo(L::F(L"dk.confirm", d.Number, d.Name, App::Gb(d.Size), current, style, GetText(lblPlan)), MB_ICONWARNING)) return;
        if (!app.ConfirmTyped(L::F(L"dk.confirm_type", d.Number), Num(d.Number), L"dk.confirm_type_title")) return;

        Job job;
        job[L"mode"] = L"disk";
        job[L"disk"] = Num(d.Number);
        job[L"disk_size"] = Num(d.Size);
        job[L"style"] = style;
        job[L"layout"] = p.Layout();
        job[L"sys_mb"] = Num(p.SysMb);
        job[L"msr"] = p.Msr ? L"1" : L"0";
        job[L"recovery_mb"] = p.Recovery ? Num(p.RecoveryMb) : L"0";
        job[L"win_pct"] = Num(p.WindowsPercent);
        job[L"label"] = p.Label;
        job[L"data_fs"] = p.DataFs;
        job[L"data_label"] = p.DataLabel;
        jobDisk = d.Number;
        jobPlan = p;
        app.job = App::JobInfo();
        app.onSuccess = [this](const wstring& elapsed) { return Done(elapsed); };
        app.onFailure = [this] { LoadDisks(); app.restore->FillTargets(); };
        app.Log(L::F(L"dk.log_start", d.Number, d.Name, style,
            L::T(!p.Windows ? L"dk.layout_data" : p.Data ? L"dk.layout_windows_data" : L"dk.layout_windows")));
        app.StartHelper(job);
    }

    // after a successful disk job; a Windows layout goes on to the restore tab.
    // The messages are shown here: no "after done" for a disk job.
    wstring DiskTab::Done(const wstring& elapsed)
    {
        LoadDisks();
        wchar_t nl = app.job.newLetter;
        wstring style = jobPlan.Gpt ? L"GPT" : L"MBR";
        wstring letter = nl ? wstring(1, nl) + L":" : L"-";
        if (!jobPlan.Windows || !nl)
        {
            app.restore->FillTargets();
            Info(L::F(L"dk.done_data", jobDisk, style, letter) + L"\r\n\r\n" + elapsed);
            return wstring();
        }
        Info(L::F(L"dk.done_windows", jobDisk, style, letter, L::T(jobPlan.Gpt ? L"dk.done_uefi" : L"dk.done_bios"))
            + (jobPlan.Data ? L"\r\n\r\n" + L::F(L"dk.done_with_data", jobPlan.DataLabelClean(), jobPlan.DataFs) : wstring())
            + L"\r\n\r\n" + elapsed);
        // the restore tab: the new partition as target; FillTargets reads the
        // partitions again, then UEFI (EFI partition) or BIOS (active
        // partition) and the system partition are chosen for its disk
        app.restore->SetLabel(jobPlan.WindowsLabel());
        app.restore->FillTargets();
        app.restore->SelectTarget(wstring(1, nl) + L":\\");
        app.win.SelectPage(app.restore->page);
        return wstring();
    }
}
