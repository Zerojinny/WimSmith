// ============================================================
//  WimSmith - tab_export.cpp
//  The export tab (see tab_export.h).
// ============================================================
#include "tab_export.h"
#include "helper.h"
#include "lang.h"
#include <algorithm>

namespace wimsmith
{
    using namespace ui;

    static const int CompTypes[] = { wl::CompressionLzx, wl::CompressionXpress, wl::CompressionLzms };

    // <folder of the source>\<name of the source>_export.wim
    static wstring DefaultExportWim(const wstring& source)
    {
        wstring full = FullPath(source);
        return full.empty() ? wstring() : Combine(DirName(full), FileStem(full) + L"_export.wim");
    }

    static wstring ThreadText(int threads)
    {
        return threads > 0 ? L::F(L"bk.threads_value", threads) : L::F(L"bk.threads_auto", (long long)App::Processors());
    }

    ExportTab::ExportTab(App& a, HWND p) : app(a), page(p), grid(p)
    {
        txtSrc = Edit(page);
        btnSrc = Button(page, L::T(L"qbt.btn_browse"));
        btnRead = Button(page, L::T(L"bk.btn_read"));
        grid.Add(L::T(L"ex.row_source"), { Field(txtSrc, 420), Btn(btnSrc, 100), Btn(btnRead, 120) });
        cboImage = Combo(page);
        grid.Add(L::T(L"bk.row_image"), { Field(cboImage, 520) });
        txtWim = Edit(page);
        btnWim = Button(page, L::T(L"qbt.btn_browse"));
        grid.Add(L::T(L"ex.row_target"), { Field(txtWim, 420), Btn(btnWim, 100) });
        txtName = Edit(page);
        lblDesc = Label(page, L::T(L"bk.row_desc"));
        txtDesc = Edit(page);
        grid.Add(L::T(L"bk.row_name"), { Field(txtName, 200), Txt(lblDesc, 8, 4), Field(txtDesc, 260) });
        cboComp = Combo(page);
        for (const wchar_t* k : { L"bk.comp_lzx", L"bk.comp_xpress", L"bk.comp_lzms" }) ComboAdd(cboComp, L::T(k));
        ComboSelect(cboComp, 0);
        lblThreads = Label(page, L::T(L"bk.row_threads"));
        numThreads = Number(page, 0, 64, 0);
        lblThreadsHint = Label(page, L::F(L"bk.threads_hint", (long long)App::Processors()), FontUI, color::Dim);
        grid.Add(L::T(L"bk.row_comp"), { Field(cboComp, 330), Txt(lblThreads, 8, 4), Field(numThreads, 60), Txt(lblThreadsHint, 0, 0) });
        rbReplace = Radio(page, L::T(L"bk.existing_replace"), 1);
        rbAppend = Radio(page, L::T(L"ex.existing_append"), 1);
        SetChecked(rbReplace, true);
        grid.Add(L::T(L"bk.row_existing"), { Box(rbReplace), Box(rbAppend) });
        chkCheck = Check(page, L::T(L"ex.check"));
        grid.Add(L::T(L"bk.row_check"), { Box(chkCheck) });
        btnExport = Button(page, L::T(L"ex.btn_export"));
        Primary(btnExport, false);
        grid.Add(L"", { Btn(btnExport, 160) });
        SetLayout(page, [this](int w, int h) { Layout(w, h); });

        OnClick(btnSrc, [this] { ChooseSource(); });
        OnClick(btnRead, [this] { ReadImages(true); });
        OnClick(btnWim, [this] { ChooseTarget(); });
        OnClick(btnExport, [this] { StartExport(); });
        // name / description: only for one image (all images keep their own)
        OnChange(cboImage, [this]
        {
            bool one = Image() != nullptr;
            Enable(txtName, one);
            Enable(txtDesc, one);
        });
        app.busyHandlers.push_back([this](bool busy) { SetBusy(busy); });
    }

    void ExportTab::Layout(int w, int)
    {
        grid.Place(0, 0, w);
    }

    void ExportTab::Refresh()
    {
        wstring def = app.DefaultWim();
        if (Trim(GetText(txtSrc)).empty() && FileExists(def)) { SetText(txtSrc, def); ReadImages(false); }
    }

    const wl::WimImage* ExportTab::Image() const
    {
        int i = ComboIndex(cboImage);
        return i >= 1 && i <= (int)images.size() ? &images[i - 1] : nullptr;
    }

    void ExportTab::ChooseSource()
    {
        wstring cur = Trim(GetText(txtSrc)), file;
        if (!OpenFile({ { L"WIM (*.wim;*.esd;*.swm)", L"*.wim;*.esd;*.swm" }, { L"*.*", L"*.*" } }, DirName(FullPath(cur)), file)) return;
        SetText(txtSrc, file);
        ReadImages(true);
    }

    void ExportTab::ChooseTarget()
    {
        wstring cur = Trim(GetText(txtWim));
        if (cur.empty()) cur = DefaultExportWim(Trim(GetText(txtSrc)));
        wstring full = FullPath(cur), file;
        // the type of the current name first: its extension is added when none is typed
        Filters wim = { { L"WIM (*.wim)", L"*.wim" }, { L"ESD (*.esd)", L"*.esd" } };
        if (EndsWithI(cur, L".esd")) std::swap(wim[0], wim[1]);
        if (!SaveFile(wim, full.empty() ? wstring() : DirName(full), full.empty() ? wstring(L"export.wim") : FileName(full), false, file)) return;
        SetText(txtWim, file);
        // an .esd file is written solid (LZMS)
        if (EndsWithI(file, L".esd")) ComboSelect(cboComp, 2);
    }

    void ExportTab::ReadImages(bool show)
    {
        images.clear();
        ComboClear(cboImage);
        wstring wim = Trim(GetText(txtSrc));
        if (wim.empty() || !FileExists(wim))
        {
            if (show) Info(L::F(L"msg.not_found", wim.empty() ? wstring(L"-") : wim), MB_ICONWARNING);
            return;
        }
        std::vector<wl::WimImage> list;
        if (!app.Images(wim, show, list)) return;
        images = list;
        ComboAdd(cboImage, L::F(L"ex.all_images", (long long)images.size()));
        for (const wl::WimImage& w : images)
            ComboAdd(cboImage, L::F(L"bk.image_line", w.Index, w.Name.empty() ? L::T(L"bk.no_name") : w.Name,
                w.TotalBytes >= 0 ? App::Gb(w.TotalBytes) : wstring(L"?"))
                + (w.Description.empty() ? wstring() : L"  - " + w.Description));
        // one image: that image; several: all of them
        ComboSelect(cboImage, images.size() == 1 ? 1 : 0);
        if (Trim(GetText(txtWim)).empty()) SetText(txtWim, DefaultExportWim(wim));
        app.Log(L::F(L"bk.log_read", wim, (long long)images.size()));
    }

    void ExportTab::SetSource(const wstring& wim, int image)
    {
        SetText(txtSrc, wim);
        SetText(txtWim, L"");
        ReadImages(false);
        if (image > 0)
            for (size_t n = 0; n < images.size(); n++)
                if (images[n].Index == image) ComboSelect(cboImage, (int)n + 1);
    }

    void ExportTab::SetBusy(bool busy)
    {
        for (HWND c : { btnExport, txtSrc, txtWim, cboImage, chkCheck }) Enable(c, !busy);
    }

    void ExportTab::StartExport()
    {
        if (app.Busy()) return;
        wstring source = Trim(GetText(txtSrc)), wim = Trim(GetText(txtWim));
        wstring why = ExportProblem(source, wim);
        if (!why.empty()) { Info(L::F(why, source.empty() ? wstring(L"-") : source, wim), MB_ICONWARNING); return; }
        int sel = ComboIndex(cboImage);
        if (sel < 0) { Info(L::T(L"bk.err_no_image"), MB_ICONWARNING); return; }
        if (!app.LoadWimlib()) return;

        const wl::WimImage* imgp = Image();
        bool one = imgp != nullptr;
        wl::WimImage img = one ? *imgp : wl::WimImage();
        wstring name = one ? Trim(GetText(txtName)) : wstring();
        wstring desc = one ? Trim(GetText(txtDesc)) : wstring();
        bool exists = FileExists(wim);
        bool append = exists && Checked(rbAppend);
        wstring what = L::T(L"bk.confirm_new");
        if (append)
        {
            // the names of the new images must not be in the target yet
            std::vector<wl::WimImage> old, src;
            if (!app.Images(wim, true, old) || !app.Images(source, true, src)) return;
            std::vector<wstring> names;
            if (one) names.push_back(name.empty() ? img.Name : name);
            else for (const wl::WimImage& w : src) names.push_back(w.Name);
            for (const wl::WimImage& w : old)
                for (const wstring& nm : names)
                    if (!nm.empty() && EqualsI(w.Name, nm)) { Info(L::F(one ? L"bk.err_name_used" : L"ex.err_names_used", nm), MB_ICONWARNING); return; }
            what = L::F(L"ex.confirm_append", (long long)old.size() + 1);
        }
        else if (exists) what = L::T(L"bk.confirm_replace");
        if (Checked(chkCheck)) what += L"\r\n" + L::T(L"ex.confirm_check");
        int threads = NumberValue(numThreads);
        wstring threadText = ThreadText(threads);
        int ci = std::max(0, std::min(2, ComboIndex(cboComp)));
        wstring imageText = ComboItem(cboImage, sel) + (one && !name.empty() ? L"  ->  " + name : wstring());
        if (!YesNo(L::F(L"ex.confirm", source, imageText, wim,
                (append ? L::T(L"bk.comp_existing") : ComboItem(cboComp, ci)) + L", " + threadText, what), MB_ICONQUESTION))
            return;

        auto oneLine = [](wstring s) { for (wchar_t& c : s) if (c == L'\r' || c == L'\n') c = L' '; return s; };
        Job job;
        job[L"mode"] = L"export";
        job[L"source"] = source;
        job[L"index"] = one ? Num(img.Index) : L"0";
        job[L"wim"] = wim;
        job[L"name"] = oneLine(name);
        job[L"description"] = oneLine(desc);
        job[L"compression"] = Num(CompTypes[ci]);
        job[L"threads"] = Num(threads);
        job[L"append"] = append ? L"1" : L"0";
        job[L"check"] = Checked(chkCheck) ? L"1" : L"0";
        app.job = App::JobInfo();
        app.job.wim = wim;
        app.job.image = one ? img.Index : 0;
        app.onSuccess = [this](const wstring& elapsed)
        {
            wstring w = app.job.wim;
            // the other tabs show the new content of the file
            app.WimWritten(w, false);
            return L::F(L"ex.done", w) + L"\r\n\r\n" + elapsed;
        };
        app.onFailure = nullptr;
        app.Log(L::F(L"ex.log_start", one ? Num(img.Index) : L::T(L"ex.all"), source, wim) + L"  (" + threadText + L")");
        app.StartHelper(job);
    }
}
