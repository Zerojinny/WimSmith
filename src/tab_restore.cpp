// ============================================================
//  WimSmith - tab_restore.cpp
//  The restore tab (see tab_restore.h).
// ============================================================
#include "tab_restore.h"
#include "helper.h"
#include "lang.h"
#include "settings.h"
#include <algorithm>
#include <thread>

namespace wimsmith
{
    using namespace ui;

    // the System Compression (Compact OS, wof.sys) is part of Windows 10 and later
    static bool Windows10OrLater()
    {
        typedef LONG(WINAPI * GetVersionFn)(PRTL_OSVERSIONINFOW);
        GetVersionFn f = (GetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
        RTL_OSVERSIONINFOW v = {};
        v.dwOSVersionInfoSize = sizeof v;
        return f && f(&v) == 0 && v.dwMajorVersion >= 10;
    }

    static const wchar_t* const CompactNames[] = { L"", L"XPRESS4K", L"XPRESS8K", L"XPRESS16K", L"LZX" };

    static wstring FirmwareText(const wstring& fw)
    {
        return fw == L"ALL" ? L"UEFI + BIOS" : fw == L"BIOS" ? L"BIOS" : L"UEFI";
    }

    RestoreTab::RestoreTab(App& a, HWND p) : app(a), page(p), grid(p)
    {
        txtWim = Edit(page);
        btnWim = Button(page, L::T(L"qbt.btn_browse"));
        btnRead = Button(page, L::T(L"bk.btn_read"));
        grid.Add(L::T(L"bk.row_wim"), { Field(txtWim, 420), Btn(btnWim, 100), Btn(btnRead, 120) });
        cboImage = Combo(page);
        grid.Add(L::T(L"bk.row_image"), { Field(cboImage, 520) });
        cboTarget = Combo(page);
        btnTgtFolder = Button(page, L::T(L"bk.btn_folder"));
        btnTgtRefresh = Button(page, L::T(L"adk.btn_refresh"));
        grid.Add(L::T(L"bk.row_target"), { Field(cboTarget, 420), Btn(btnTgtFolder, 110), Btn(btnTgtRefresh, 90) });
        txtLabel = Edit(page);
        SetText(txtLabel, L"Windows");
        grid.Add(L::T(L"bk.row_label"), { Field(txtLabel, 200) });
        chkUefi = Check(page, L::T(L"bk.boot_uefi"));
        chkBios = Check(page, L::T(L"bk.boot_bios"));
        chkNoBoot = Check(page, L::T(L"bk.boot_none"));
        SetChecked(chkNoBoot, true);          // until the target disk is known
        lblBootLang = Label(page, L::T(L"bk.boot_lang"));
        // other locales can be typed
        cboBootLang = Combo(page, true);
        for (const wchar_t* l : { L"ko-kr", L"en-us", L"ja-jp", L"zh-cn", L"zh-tw", L"de-de", L"fr-fr", L"es-es", L"ru-ru" }) ComboAdd(cboBootLang, l);
        // the last language used (WimSmith.ini: restore.boot_lang)
        wstring saved = Lower(GetSetting(L"restore.boot_lang", L"ko-kr"));
        SetWindowTextW(cboBootLang, IsLocale(saved) ? saved.c_str() : L"ko-kr");
        grid.Add(L::T(L"bk.row_boot"), { Box(chkUefi), Box(chkBios), Box(chkNoBoot), Txt(lblBootLang, 16, 4), Field(cboBootLang, 90) });
        cboSysPart = Combo(page);
        btnPartRefresh = Button(page, L::T(L"adk.btn_refresh"));
        grid.Add(L::T(L"bk.row_syspart"), { Field(cboSysPart, 520), Btn(btnPartRefresh, 90) });
        // Compact OS: the files compressed by Windows (wimlib --compact); the last choice is kept
        cboCompact = Combo(page);
        for (const wchar_t* k : { L"rs.compact_off", L"rs.compact_x4", L"rs.compact_x8", L"rs.compact_x16", L"rs.compact_lzx" }) ComboAdd(cboCompact, L::T(k));
        long long compact = ParseLong(GetSetting(L"restore.compact", L"0"), 0);
        bool win10 = Windows10OrLater();
        ComboSelect(cboCompact, win10 && compact >= 0 && compact <= 4 ? (int)compact : 0);
        if (!win10) Tooltip(cboCompact, L::T(L"rs.compact_needs_10"));
        grid.Add(L::T(L"rs.row_compact"), { Field(cboCompact, 320) });
        // wimlib --check: the WIM file is checked against its integrity table first (off by default)
        chkCheck = Check(page, L::T(L"rs.check"));
        grid.Add(L::T(L"bk.row_check"), { Box(chkCheck) });
        btnApply = Button(page, L::T(L"bk.btn_apply"));
        Primary(btnApply, true);
        grid.Add(L"", { Btn(btnApply, 160) });
        SetLayout(page, [this](int w, int h) { Layout(w, h); });

        OnClick(btnWim, [this] { ChooseWim(); });
        OnClick(btnRead, [this] { ReadImages(true); });
        OnClick(btnTgtFolder, [this] { ChooseTargetFolder(); });
        OnClick(btnTgtRefresh, [this] { FillTargets(); });
        // the volume label and the boot files: only when a volume is formatted
        OnChange(cboTarget, [this]
        {
            const Volume* t = Target();
            Enable(txtLabel, !t || t->Letter != 0);
            UpdateBoot();
        });
        // None excludes UEFI / BIOS (UEFI + BIOS together = both boot types)
        for (HWND box : { chkUefi, chkBios, chkNoBoot }) OnChange(box, [this, box] { BootBoxChanged(box); });
        OnClick(btnPartRefresh, [this] { LoadPartitions(); });
        OnClick(btnApply, [this] { StartApply(); });
        OnChange(cboCompact, [this] { SetSetting(L"restore.compact", Num(std::max(0, ComboIndex(cboCompact)))); });
        // a valid boot manager language is kept for the next start (after the
        // combo box has taken the chosen text)
        OnChange(cboBootLang, [this]
        {
            Post(app.win.hwnd, [this]
            {
                wstring l = Lower(Trim(GetText(cboBootLang)));
                if (IsLocale(l) && l != GetSetting(L"restore.boot_lang", L"")) SetSetting(L"restore.boot_lang", l);
            });
        });
        app.busyHandlers.push_back([this](bool busy) { SetBusy(busy); });
    }

    void RestoreTab::Layout(int w, int)
    {
        grid.Place(0, 0, w);
    }

    void RestoreTab::Refresh()
    {
        // the last WIM file of this tab (restore.wim), else the one of the last backup
        wstring def = RestoreWim();
        if (def.empty() || !FileExists(def)) def = app.DefaultWim();
        if (Trim(GetText(txtWim)).empty() && FileExists(def)) { SetText(txtWim, def); ReadImages(false); }
        else FillTargets();
    }

    const Volume* RestoreTab::Target() const
    {
        int i = ComboIndex(cboTarget);
        return i >= 0 && i < (int)targets.size() ? &targets[i] : nullptr;
    }

    const wl::WimImage* RestoreTab::Image() const
    {
        int i = ComboIndex(cboImage);
        return i >= 0 && i < (int)images.size() ? &images[i] : nullptr;
    }

    wstring RestoreTab::ImageText(int index) const
    {
        return index >= 0 && index < ComboCount(cboImage) ? ComboItem(cboImage, index) : wstring();
    }

    // ---------- targets ----------

    // a chosen folder, plus every local volume that may be formatted
    void RestoreTab::FillTargets()
    {
        const Volume* cur = Target();
        wstring keep = cur ? cur->Path : wstring();
        wstring wim = Trim(GetText(txtWim));
        std::vector<Volume> list;
        for (const Volume& v : targets) if (v.Letter == 0) list.push_back(v);
        int volumes = 0;
        for (const Volume& v : App::Volumes(false))
            if (TargetProblem(v.Letter, wim, app.root).empty()) { list.push_back(v); volumes++; }
        targets.clear();
        ComboClear(cboTarget);
        targets = list;
        for (const Volume& v : targets) ComboAdd(cboTarget, v.Text);
        SelectTarget(keep);
        if (volumes == 0) app.Log(L::T(L"bk.no_targets"));
        LoadPartitions();
    }

    void RestoreTab::SelectTarget(const wstring& path)
    {
        for (size_t n = 0; n < targets.size(); n++)
            if (EqualsI(targets[n].Path, path)) { ComboSelect(cboTarget, (int)n); return; }
    }

    void RestoreTab::ChooseTargetFolder()
    {
        wstring folder;
        if (!PickFolder(L::T(L"bk.select_target"), wstring(), folder)) return;
        wstring why = FolderTargetProblem(folder, Trim(GetText(txtWim)), app.root);
        if (!why.empty()) { Info(L::F(why, folder), MB_ICONWARNING); return; }
        Volume v;
        v.Path = folder;
        v.Text = L::F(L"bk.target_folder", folder);
        std::vector<Volume> list(1, v);
        for (const Volume& t : targets) if (t.Letter != 0) list.push_back(t);
        targets.clear();
        ComboClear(cboTarget);
        targets = list;
        for (const Volume& t : targets) ComboAdd(cboTarget, t.Text);
        ComboSelect(cboTarget, 0);
    }

    // ---------- images ----------

    void RestoreTab::ChooseWim()
    {
        wstring cur = Trim(GetText(txtWim)), file;
        // a split WIM: any of its parts (the others are found in its folder)
        if (!OpenFile({ { L"WIM (*.wim;*.esd;*.swm)", L"*.wim;*.esd;*.swm" }, { L"*.*", L"*.*" } }, DirName(FullPath(cur)), file)) return;
        SetText(txtWim, file);
        ReadImages(true);
    }

    void RestoreTab::ReadImages(bool show)
    {
        images.clear();
        ComboClear(cboImage);
        wstring wim = Trim(GetText(txtWim));
        FillTargets();
        if (wim.empty() || !FileExists(wim))
        {
            if (show) Info(L::F(L"msg.not_found", wim.empty() ? wstring(L"-") : wim), MB_ICONWARNING);
            return;
        }
        if (!app.LoadWimlib()) return;
        std::vector<wl::WimImage> list;
        int total = 1, found = 1;
        int rc = wl::ReadImages(wim, list, total, found);
        if (rc != 0)
        {
            wstring err = rc == wl::ErrPartsMissing ? L::F(L"sj.err_part_count", wim, total, found) : wl::ErrorText(rc);
            wstring text = L::F(L"bk.read_failed", wim, err);
            wstring one = text;
            for (size_t p; (p = one.find(L"\r\n")) != wstring::npos;) one.replace(p, 2, L" ");
            app.Log(one);
            if (show) Info(text, MB_ICONWARNING);
            return;
        }
        images = list;
        for (const wl::WimImage& w : images)
            ComboAdd(cboImage, L::F(L"bk.image_line", w.Index, w.Name.empty() ? L::T(L"bk.no_name") : w.Name,
                w.TotalBytes >= 0 ? App::Gb(w.TotalBytes) : wstring(L"?"))
                + (w.Description.empty() ? wstring() : L"  - " + w.Description));
        if (!images.empty()) ComboSelect(cboImage, 0);
        app.Log(L::F(L"bk.log_read", wim, (long long)images.size()));
        wstring full = FullPath(wim);
        if (!full.empty()) SetRestoreWim(full);
    }

    void RestoreTab::SetWim(const wstring& wim, int image)
    {
        SetText(txtWim, wim);
        ReadImages(false);
        for (size_t n = 0; n < images.size(); n++)
            if (images[n].Index == image) ComboSelect(cboImage, (int)n);
    }

    void RestoreTab::SetLabel(const wstring& label) { SetText(txtLabel, label); }

    // ---------- boot files: firmware and system partition ----------

    // Get-Partition takes a few seconds: read in the background
    void RestoreTab::LoadPartitions()
    {
        if (partsLoading) return;
        partsLoading = true;
        Enable(btnPartRefresh, false);
        sysItems.clear();
        ComboClear(cboSysPart);
        sysItems.push_back(SysItem());
        ComboAdd(cboSysPart, L::T(L"bk.syspart_reading"));
        ComboSelect(cboSysPart, 0);
        HWND target = app.win.hwnd;
        std::thread([this, target]
        {
            std::vector<DiskPartition> list;
            wstring err;
            bool ok = QueryPartitions(list, err);
            Post(target, [this, list, err, ok]
            {
                partsLoading = false;
                Enable(btnPartRefresh, !app.Busy());
                partitions = list;
                partsKnown = ok;
                if (!ok) app.Log(L::F(L"bk.syspart_failed", err));
                UpdateBoot();
            });
        }).detach();
    }

    const DiskPartition* RestoreTab::TargetPartition() const
    {
        const Volume* t = Target();
        if (!t || t->Letter == 0 || !partsKnown) return nullptr;
        for (const DiskPartition& p : partitions)
            if (p.Letter == t->Letter) return &p;
        return nullptr;
    }

    void RestoreTab::BootBoxChanged(HWND box)
    {
        if (bootSync) return;
        bootSync = true;
        if (box == chkNoBoot && Checked(chkNoBoot)) { SetChecked(chkUefi, false); SetChecked(chkBios, false); }
        else if (box != chkNoBoot && Checked(box)) SetChecked(chkNoBoot, false);
        // nothing checked = None
        if (!Checked(chkUefi) && !Checked(chkBios)) SetChecked(chkNoBoot, true);
        bootSync = false;
        UpdateBoot();
    }

    // "" = None: no boot files
    wstring RestoreTab::Firmware() const
    {
        if (Checked(chkNoBoot)) return wstring();
        if (Checked(chkUefi) && Checked(chkBios)) return L"ALL";
        if (Checked(chkUefi)) return L"UEFI";
        if (Checked(chkBios)) return L"BIOS";
        return wstring();
    }

    void RestoreTab::AddSysPart(const DiskPartition& p, const DiskPartition& target)
    {
        wstring kind = p.Offset == target.Offset && p.Disk == target.Disk ? L::T(L"bk.syspart_target")
            : p.IsEsp() ? L::T(L"bk.syspart_esp") : p.Active ? L::T(L"bk.syspart_active") : wstring();
        wstring size = p.Size >= 1073741824LL ? App::Gb(p.Size) : Num(p.Size / MiB) + L" MB";
        SysItem it;
        it.has = true;
        it.part = p;
        sysItems.push_back(it);
        ComboAdd(cboSysPart, L::F(L"bk.syspart_line", p.Disk, p.Number, kind.empty() ? wstring(L"-") : kind,
            p.FileSystem.empty() ? wstring(L"RAW") : p.FileSystem, size,
            p.Letter ? wstring(1, p.Letter) + L":" : L::T(L"bk.syspart_no_letter")));
    }

    // a new target: the firmware boxes and the system partition are chosen
    // again for its disk (refreshing the lists keeps them)
    void RestoreTab::UpdateBoot()
    {
        if (partsLoading) return;
        const Volume* t = Target();
        bool volume = t && t->Letter != 0 && !app.Busy();
        const DiskPartition* tpp = TargetPartition();
        DiskPartition tp = tpp ? *tpp : DiskPartition();
        wstring key = tpp ? Num(tp.Disk) + L":" + Num(tp.Offset) : wstring();
        bool newTarget = key != bootTargetKey;
        bootTargetKey = key;
        if (newTarget && tpp)
        {
            bool esp = false;
            for (const DiskPartition& p : partitions)
                if (p.Disk == tp.Disk && p.IsEsp() && !p.IsSystem) esp = true;
            bootSync = true;
            SetChecked(chkUefi, esp || tp.Gpt());
            SetChecked(chkBios, !Checked(chkUefi));
            SetChecked(chkNoBoot, false);
            bootSync = false;
        }
        for (HWND b : { chkUefi, chkBios, chkNoBoot }) Enable(b, volume);
        wstring fw = Firmware();

        int oldIndex = ComboIndex(cboSysPart);
        SysItem old = oldIndex >= 0 && oldIndex < (int)sysItems.size() ? sysItems[oldIndex] : SysItem();
        sysItems.clear();
        ComboClear(cboSysPart);
        if (tpp)
        {
            // the disk of the target first, then the EFI / active partitions of other disks
            for (const DiskPartition& p : partitions)
                if (p.Disk == tp.Disk && !p.IsMsr() && !p.IsSystem) AddSysPart(p, tp);
            for (const DiskPartition& p : partitions)
                if (p.Disk != tp.Disk && (p.IsEsp() || p.Active) && !p.IsSystem) AddSysPart(p, tp);
        }
        int pick = -1;
        bool sameFw = fw == bootFw;
        bootFw = fw;
        if (!newTarget && sameFw && old.has)
            for (size_t n = 0; n < sysItems.size(); n++)
                if (sysItems[n].part.Disk == old.part.Disk && sysItems[n].part.Offset == old.part.Offset) pick = (int)n;
        if (pick < 0 && tpp) pick = DefaultSysPart(tp, fw);
        if (pick < 0 && !sysItems.empty())
        {
            sysItems.insert(sysItems.begin(), SysItem());
            SendMessageW(cboSysPart, CB_INSERTSTRING, 0, (LPARAM)L::T(L"bk.syspart_choose").c_str());
            pick = 0;
        }
        if (sysItems.empty())
        {
            sysItems.push_back(SysItem());
            ComboAdd(cboSysPart, L::T(!partsKnown ? L"bk.syspart_unknown" : !t || t->Letter == 0 ? L"bk.syspart_volume_only" : L"bk.syspart_none"));
        }
        ComboSelect(cboSysPart, std::max(0, pick));
        Enable(cboSysPart, volume && !fw.empty() && tpp);
        Enable(cboBootLang, volume && !fw.empty());
    }

    // UEFI: the EFI partition of the target disk; BIOS: its system reserved
    // partition (a small active partition), else the target itself (the
    // helper makes it active)
    static const long long ReservedMax = 4096LL * 1048576;   // the largest system partition of the disk tab
    int RestoreTab::DefaultSysPart(const DiskPartition& tp, const wstring& fw) const
    {
        int esp = -1, active = -1, self = -1;
        for (size_t n = 0; n < sysItems.size(); n++)
        {
            const DiskPartition& p = sysItems[n].part;
            if (!sysItems[n].has || p.Disk != tp.Disk) continue;
            if (p.IsEsp() && esp < 0) esp = (int)n;
            if (p.Active && active < 0 && p.Offset != tp.Offset && p.Size <= ReservedMax) active = (int)n;
            if (p.Offset == tp.Offset) self = (int)n;
        }
        if (fw == L"BIOS") return active >= 0 ? active : self;
        return esp;
    }

    // ---------- the job ----------

    void RestoreTab::SetBusy(bool busy)
    {
        for (HWND c : { btnApply, cboTarget, cboImage, txtWim, btnPartRefresh, chkCheck }) Enable(c, !busy);
        Enable(cboCompact, !busy && Windows10OrLater());
        if (busy) for (HWND c : { chkUefi, chkBios, chkNoBoot, cboSysPart, cboBootLang }) Enable(c, false);
        else UpdateBoot();
    }

    int RestoreTab::Compact() const
    {
        int c = ComboIndex(cboCompact);
        return c >= 1 && c <= 4 && Windows10OrLater() ? c : 0;
    }

    void RestoreTab::StartApply()
    {
        if (app.Busy()) return;
        wstring wim = Trim(GetText(txtWim));
        const wl::WimImage* imgp = Image();
        const Volume* tgtp = Target();
        if (wim.empty() || !FileExists(wim)) { Info(L::F(L"msg.not_found", wim.empty() ? wstring(L"-") : wim), MB_ICONWARNING); return; }
        if (!imgp) { Info(L::T(L"bk.err_no_image"), MB_ICONWARNING); return; }
        if (!tgtp) { Info(L::T(L"bk.err_no_target"), MB_ICONWARNING); return; }
        wl::WimImage img = *imgp;
        Volume tgt = *tgtp;
        wstring imgText = ImageText(ComboIndex(cboImage));
        if (tgt.Letter == 0) { StartApplyToFolder(wim, img, imgText, tgt.Path); return; }
        wstring letter(1, tgt.Letter);
        wstring why = TargetProblem(tgt.Letter, wim, app.root);
        if (!why.empty()) { Info(L::F(why, tgt.Letter), MB_ICONWARNING); FillTargets(); return; }
        // compressed (Compact OS) the image needs less than its size
        if (img.TotalBytes > 0 && tgt.Size > 0 && tgt.Size < img.TotalBytes && Compact() == 0)
        {
            Info(L::F(L"bk.err_too_small", App::Gb(img.TotalBytes), tgt.Letter, App::Gb(tgt.Size)), MB_ICONWARNING);
            return;
        }
        // boot files: firmware and system partition
        wstring fw = Firmware();
        bool hasSys = false;
        DiskPartition sys;
        wstring sysText;
        if (!fw.empty())
        {
            int si = ComboIndex(cboSysPart);
            if (si >= 0 && si < (int)sysItems.size() && sysItems[si].has) { hasSys = true; sys = sysItems[si].part; sysText = ComboItem(cboSysPart, si); }
            if (!hasSys) { Info(L::T(L"bk.err_no_syspart"), MB_ICONWARNING); return; }
            DiskPartition check = sys;
            // the target itself: NTFS after the format
            if (sys.Letter == tgt.Letter) check.FileSystem = L"NTFS";
            wstring sysWhy = SystemPartitionProblem(check, fw);
            if (!sysWhy.empty()) { Info(L::F(sysWhy, sys.Disk, sys.Number, fw), MB_ICONWARNING); return; }
        }
        wstring bootLang = Lower(Trim(GetText(cboBootLang)));
        if (hasSys && !IsLocale(bootLang)) { Info(L::F(L"bk.err_boot_lang", bootLang.empty() ? wstring(L"-") : bootLang), MB_ICONWARNING); return; }
        if (!app.LoadWimlib()) return;

        wstring windows = DirExists(letter + L":\\Windows") ? L"\r\n\r\n" + L::F(L"bk.confirm_apply_windows", tgt.Letter) : wstring();
        windows += L"\r\n\r\n" + (hasSys ? L::F(L"bk.confirm_boot", FirmwareText(fw) + L", " + bootLang, sysText) : L::T(L"bk.confirm_no_boot"));
        // BIOS on MBR starts the active partition: the helper makes the system partition active
        if (hasSys && fw != L"UEFI" && !sys.Gpt() && !sys.Active)
            windows += L"\r\n" + L::F(L"bk.confirm_make_active", sys.Disk, sys.Number);
        if (Checked(chkCheck)) windows += L"\r\n\r\n" + L::T(L"rs.confirm_check");
        if (Compact()) windows += L"\r\n\r\n" + L::F(L"rs.confirm_compact", CompactNames[Compact()]);
        if (!YesNo(L::F(L"bk.confirm_apply", tgt.Letter, tgt.Text, imgText, wim, windows), MB_ICONWARNING)) return;
        if (!app.ConfirmTyped(L::F(L"bk.confirm_type", tgt.Letter), letter)) return;

        Job job;
        job[L"mode"] = L"apply";
        job[L"wim"] = wim;
        job[L"index"] = Num(img.Index);
        job[L"target"] = letter;
        job[L"check"] = Checked(chkCheck) ? L"1" : L"0";
        job[L"compact"] = Num(Compact());
        job[L"label"] = CleanLabel(GetText(txtLabel));
        if (hasSys)
        {
            job[L"boot"] = fw;
            job[L"boot_lang"] = bootLang;
            job[L"sys_disk"] = Num(sys.Disk);
            job[L"sys_part"] = Num(sys.Number);
            job[L"sys_offset"] = Num(sys.Offset);
        }
        app.job = App::JobInfo();
        app.job.boot = hasSys ? fw : wstring();
        app.job.wim = wim;
        app.job.image = img.Index;
        app.job.target = letter;
        app.onSuccess = [this](const wstring& elapsed)
        {
            const App::JobInfo& j = app.job;
            wstring msg = L::F(j.ToFolder() ? L"bk.apply_done_folder" : !j.boot.empty() ? L"bk.apply_done_boot" : L"bk.apply_done",
                j.image, j.target, FirmwareText(j.boot)) + L"\r\n\r\n" + elapsed;
            FillTargets();
            return msg;
        };
        app.onFailure = nullptr;
        app.Log(L::F(L"bk.log_start_apply", img.Index, wim, tgt.Letter));
        app.StartHelper(job);
    }

    // folder target: no format; the folder has to be empty (or new)
    void RestoreTab::StartApplyToFolder(const wstring& wim, const wl::WimImage& img, const wstring& imgText, const wstring& folder)
    {
        wstring why = FolderTargetProblem(folder, wim, app.root);
        if (!why.empty()) { Info(L::F(why, folder), MB_ICONWARNING); return; }
        long long free = DriveFreeBytes(Root(folder));
        if (img.TotalBytes > 0 && free >= 0 && free < img.TotalBytes && Compact() == 0)
        {
            Info(L::F(L"bk.err_folder_too_small", App::Gb(img.TotalBytes), folder, App::Gb(free)), MB_ICONWARNING);
            return;
        }
        if (!app.LoadWimlib()) return;
        wstring question = L::F(L"bk.confirm_apply_folder", folder, imgText, wim);
        if (Checked(chkCheck)) question += L"\r\n\r\n" + L::T(L"rs.confirm_check_folder");
        if (Compact()) question += L"\r\n\r\n" + L::F(L"rs.confirm_compact", CompactNames[Compact()]);
        if (!YesNo(question, MB_ICONQUESTION)) return;

        Job job;
        job[L"mode"] = L"apply";
        job[L"wim"] = wim;
        job[L"index"] = Num(img.Index);
        job[L"folder"] = folder;
        job[L"check"] = Checked(chkCheck) ? L"1" : L"0";
        job[L"compact"] = Num(Compact());
        app.job = App::JobInfo();
        app.job.wim = wim;
        app.job.image = img.Index;
        app.job.target = folder;
        app.onSuccess = [this](const wstring& elapsed)
        {
            const App::JobInfo& j = app.job;
            wstring msg = L::F(L"bk.apply_done_folder", j.image, j.target, FirmwareText(j.boot)) + L"\r\n\r\n" + elapsed;
            FillTargets();
            return msg;
        };
        app.onFailure = nullptr;
        app.Log(L::F(L"bk.log_start_apply_folder", img.Index, wim, folder));
        app.StartHelper(job);
    }
}
