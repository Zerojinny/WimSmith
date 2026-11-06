// ============================================================
//  7z_wimlib - tab_info.cpp
//  The info tab (see tab_info.h).
// ============================================================
#include "tab_info.h"
#include "lang.h"
#include "tab_export.h"
#include "tab_restore.h"
#include <algorithm>

namespace dvt
{
    using namespace ui;

    static wstring CompressionName(int c)
    {
        switch (c)
        {
        case 0: return L::T(L"in.comp_none");
        case 1: return L"XPRESS";
        case 2: return L"LZX";
        case 3: return L"LZMS";
        default: return Num(c);
        }
    }

    static wstring SizeText(long long bytes)
    {
        if (bytes < 0) return L"?";
        if (bytes >= 1073741824LL) return App::Gb(bytes);
        wchar_t b[64];
        swprintf_s(b, L"%.1f MB", bytes / 1048576.0);
        return b;
    }

    // a FILETIME (UTC) as local "yyyy-MM-dd HH:mm"
    static wstring When(long long ft)
    {
        if (ft <= 0) return wstring();
        FILETIME u, l;
        u.dwLowDateTime = (DWORD)ft;
        u.dwHighDateTime = (DWORD)(ft >> 32);
        SYSTEMTIME s;
        if (!FileTimeToLocalFileTime(&u, &l) || !FileTimeToSystemTime(&l, &s)) return wstring();
        wchar_t b[32];
        swprintf_s(b, L"%04d-%02d-%02d %02d:%02d", s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute);
        return b;
    }

    // the edition of an image: FLAGS (e.g. Professional), else EDITIONID
    static wstring Edition(const wl::WimImage& w) { return !w.Flags.empty() ? w.Flags : w.EditionId; }

    InfoTab::InfoTab(App& a, HWND p) : app(a), page(p), grid(p)
    {
        txtWim = Edit(page);
        btnWim = Button(page, L::T(L"qbt.btn_browse"));
        btnRead = Button(page, L::T(L"in.btn_read"));
        grid.Add(L::T(L"bk.row_wim"), { Field(txtWim, 420), Btn(btnWim, 100), Btn(btnRead, 120) });
        lblSummary = Label(page, L::T(L"in.none"), FontUI, color::Text, SS_LEFT);
        list = List(page, {
            { L::T(L"in.col_index"), 36 }, { L::T(L"in.col_name"), 190 }, { L::T(L"in.col_edition"), 130 },
            { L::T(L"in.col_arch"), 76 }, { L::T(L"in.col_version"), 110 }, { L::T(L"in.col_lang"), 90 },
            { L::T(L"in.col_size"), 76 }, { L::T(L"in.col_files"), 120 }, { L::T(L"in.col_created"), 130 },
            { L::T(L"in.col_modified"), 130 }, { L::T(L"in.col_desc"), 220 } });
        btnRestore = Button(page, L::T(L"in.btn_restore"));
        btnExport = Button(page, L::T(L"in.btn_export"));
        btnBrowse = Button(page, L::T(L"in.btn_browse"));
        btnCopy = Button(page, L::T(L"in.btn_copy"));
        btnVerify = Button(page, L::T(L"vf.btn"));
        for (HWND b : { btnRestore, btnExport, btnBrowse, btnVerify }) Enable(b, false);
        SetLayout(page, [this](int w, int h) { Layout(w, h); });

        OnClick(btnWim, [this] { ChooseWim(); });
        OnClick(btnRead, [this] { ReadInfo(true); });
        OnChange(list, [this] { UpdateButtons(); });
        OnListDoubleClick(list, [this] { const wl::WimImage* w = Selected(); if (w) Info(Details(*w)); });
        OnClick(btnRestore, [this] { ToRestore(); });
        OnClick(btnExport, [this] { ToExport(); });
        OnClick(btnBrowse, [this] { ToBrowse(); });
        OnClick(btnCopy, [this] { Copy(); });
        OnClick(btnVerify, [this] { StartVerify(); });
        app.busyHandlers.push_back([this](bool busy) { Enable(btnVerify, !busy && !infoWim.empty()); });
    }

    void InfoTab::Layout(int w, int h)
    {
        // rows: the file (34), the summary (40), the list (the rest), the buttons (36)
        int y = grid.Place(0, 0, w);
        Move(lblSummary, 0, y, w, S(40));
        y += S(40);
        int listH = std::max(0, h - y - S(36));
        Move(list, 0, y, w, listH);
        std::vector<Item> buttons = { Btn(btnRestore, 150), Btn(btnExport, 150), Btn(btnBrowse, 150), Btn(btnCopy, 120), Btn(btnVerify, 120) };
        Grid::PlaceRow(buttons, 0, y + listH + S(4), w);
    }

    void InfoTab::Refresh()
    {
        wstring def = app.DefaultWim();
        if (Trim(GetText(txtWim)).empty() && FileExists(def)) { SetText(txtWim, def); ReadInfo(false); }
    }

    const wl::WimImage* InfoTab::Selected() const
    {
        int i = ListSelected(list);
        return hasInfo && i >= 0 && i < (int)info.Images.size() ? &info.Images[i] : nullptr;
    }

    void InfoTab::UpdateButtons()
    {
        bool one = Selected() != nullptr;
        for (HWND b : { btnRestore, btnExport, btnBrowse }) Enable(b, one);
    }

    void InfoTab::ChooseWim()
    {
        wstring cur = Trim(GetText(txtWim)), file;
        if (!OpenFile({ { L"WIM (*.wim;*.esd;*.swm)", L"*.wim;*.esd;*.swm" }, { L"*.*", L"*.*" } }, DirName(FullPath(cur)), file)) return;
        SetText(txtWim, file);
        ReadInfo(true);
    }

    void InfoTab::ReadInfo(bool show)
    {
        ListClear(list);
        hasInfo = false;
        info = wl::WimFileInfo();
        infoWim.clear();
        for (HWND b : { btnRestore, btnExport, btnBrowse, btnVerify }) Enable(b, false);
        SetText(lblSummary, L::T(L"in.none"));
        wstring wim = Trim(GetText(txtWim));
        if (wim.empty() || !FileExists(wim))
        {
            if (show) Info(L::F(L"msg.not_found", wim.empty() ? wstring(L"-") : wim), MB_ICONWARNING);
            return;
        }
        if (!app.LoadWimlib()) return;
        HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
        wl::WimFileInfo f;
        int rc = wl::ReadInfo(wim, f);
        SetCursor(old);
        if (rc != 0)
        {
            wstring text = L::F(L"bk.read_failed", wim, wl::ErrorText(rc)), one = text;
            for (size_t q; (q = one.find(L"\r\n")) != wstring::npos;) one.replace(q, 2, L" ");
            app.Log(one);
            if (show) Info(text, MB_ICONWARNING);
            return;
        }
        info = f;
        hasInfo = true;
        infoWim = wim;
        Enable(btnVerify, !app.Busy());
        SetText(lblSummary, Summary());
        for (const wl::WimImage& w : info.Images)
            ListAdd(list, {
                Num(w.Index), !w.Name.empty() ? w.Name : L::T(L"bk.no_name"), Edition(w), w.Arch, w.Version,
                !w.DefaultLanguage.empty() ? w.DefaultLanguage : w.Languages, SizeText(w.TotalBytes),
                w.FileCount >= 0 ? L::F(L"in.files_value", App::Group(w.FileCount), App::Group(std::max(0LL, w.DirCount))) : wstring(),
                When(w.Created), When(w.Modified), w.Description }, w.Index == info.BootIndex);
        if (!info.Images.empty()) ListSelect(list, 0);
        app.Log(L::F(L"bk.log_read", wim, info.ImageCount));
    }

    // two lines: images, size, compression; version, parts, boot image, flags, GUID
    wstring InfoTab::Summary() const
    {
        const wl::WimFileInfo& f = info;
        wstring comp = CompressionName(f.Compression) + (f.Solid() ? L" " + L::T(L"in.solid") : wstring());
        if (f.ChunkSize > 0) comp += L", " + L::F(L"in.chunk", f.ChunkSize / 1024);
        std::vector<wstring> flags;
        if (f.Integrity) flags.push_back(L::T(L"in.flag_integrity"));
        if (f.Pipable) flags.push_back(L::T(L"in.flag_pipable"));
        if (f.ReadOnlyFlag) flags.push_back(L::T(L"in.flag_readonly"));
        if (f.Spanned || f.TotalParts > 1) flags.push_back(L::F(L"in.flag_part", f.PartNumber, f.TotalParts));
        wstring flagText;
        for (const wstring& s : flags) flagText += (flagText.empty() ? L"" : L", ") + s;
        wchar_t ver[16];
        swprintf_s(ver, L"0x%X", f.Version);
        return L::F(L"in.summary1", f.ImageCount, SizeText(f.FileBytes >= 0 ? f.FileBytes : f.TotalBytes), comp,
                f.BootIndex > 0 ? Num(f.BootIndex) : L::T(L"in.boot_none"))
            + L"\r\n" + L::F(L"in.summary2", ver, f.Guid, flagText.empty() ? wstring(L"-") : flagText);
    }

    // all properties of one image
    wstring InfoTab::Details(const wl::WimImage& w) const
    {
        wstring s;
        auto add = [&s](const wchar_t* key, const wstring& value) { if (!value.empty()) s += L::T(key) + L": " + value + L"\r\n"; };
        add(L"in.col_index", Num(w.Index));
        add(L"in.col_name", w.Name);
        add(L"in.col_desc", w.Description);
        add(L"in.d_display", w.DisplayName);
        add(L"in.d_product", w.ProductName);
        add(L"in.col_edition", Edition(w));
        add(L"in.d_install", w.InstallationType);
        add(L"in.col_arch", w.Arch);
        add(L"in.col_version", w.Version);
        add(L"in.d_default_lang", w.DefaultLanguage);
        add(L"in.col_lang", w.Languages);
        add(L"in.col_size", w.TotalBytes >= 0 ? SizeText(w.TotalBytes) + L"  (" + App::Group(w.TotalBytes) + L" bytes)" : wstring());
        add(L"in.d_files", w.FileCount >= 0 ? App::Group(w.FileCount) : wstring());
        add(L"in.d_dirs", w.DirCount >= 0 ? App::Group(w.DirCount) : wstring());
        add(L"in.col_created", When(w.Created));
        add(L"in.col_modified", When(w.Modified));
        return s;
    }

    // the whole info (file and every image) to the clipboard
    void InfoTab::Copy()
    {
        if (!hasInfo) { Info(L::T(L"in.none"), MB_ICONWARNING); return; }
        wstring s = infoWim + L"\r\n" + Summary() + L"\r\n";
        for (const wl::WimImage& w : info.Images) s += L"\r\n" + Details(w);
        if (!OpenClipboard(app.win.hwnd)) { Info(ErrorMessage(GetLastError()), MB_ICONWARNING); return; }
        EmptyClipboard();
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
        if (g)
        {
            memcpy(GlobalLock(g), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(g);
            if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
        }
        CloseClipboard();
        app.Status(L::T(L"in.copied"));
    }

    void InfoTab::ToRestore()
    {
        const wl::WimImage* w = Selected();
        if (!w || app.Busy()) return;
        app.restore->SetWim(infoWim, w->Index);
        app.win.SelectPage(app.restore->page);
    }

    void InfoTab::ToExport()
    {
        const wl::WimImage* w = Selected();
        if (!w || app.Busy()) return;
        app.exportTab->SetSource(infoWim, w->Index);
        app.win.SelectPage(app.exportTab->page);
    }

    void InfoTab::ToBrowse()
    {
        const wl::WimImage* w = Selected();
        if (!w) return;
        app.Browse(infoWim, info.ImageCount > 1 ? w->Index : 0);
    }

    // ---------- verify (helper without administrator rights; nothing is written) ----------

    void InfoTab::StartVerify()
    {
        if (app.Busy() || infoWim.empty()) return;
        if (!FileExists(infoWim)) { Info(L::F(L"msg.not_found", infoWim), MB_ICONWARNING); return; }
        if (!app.LoadWimlib()) return;
        Job job;
        job[L"mode"] = L"verify";
        job[L"wim"] = infoWim;
        app.job = App::JobInfo();
        app.job.wim = infoWim;
        app.job.integrity = hasInfo && info.Integrity;
        app.onSuccess = [this](const wstring& elapsed)
        {
            return L::F(L"vf.done", app.job.wim, L::T(app.job.integrity ? L"vf.with_table" : L"vf.no_table")) + L"\r\n\r\n" + elapsed;
        };
        app.onFailure = nullptr;
        app.Log(L::F(L"vf.log_start", infoWim));
        app.StartHelper(job);
    }
}
