// ============================================================
//  WimSmith - tab_backup.cpp
//  The backup tab (see tab_backup.h).
// ============================================================
#include "tab_backup.h"
#include "helper.h"
#include "lang.h"
#include "settings.h"
#include "tab_restore.h"
#include <algorithm>

namespace wimsmith
{
    using namespace ui;

    static const int CompTypes[] = { wl::CompressionLzx, wl::CompressionXpress, wl::CompressionLzms };
    static const wchar_t* const CompNames[] = { L"LZX", L"XPRESS", L"LZMS" };

    // the exclusions offered first: data Windows makes again by itself
    static const wchar_t* const RecommendedExclusions[] = {
        L"\\Windows\\Temp\\*",
        L"\\Windows\\SoftwareDistribution\\Download\\*",
        L"\\Windows\\Prefetch\\*",
        L"\\ProgramData\\Microsoft\\Windows\\WER\\*",
        L"\\Users\\*\\AppData\\Local\\Temp\\*",
        L"\\Users\\*\\AppData\\Local\\Microsoft\\Windows\\INetCache\\*",
        L"\\Users\\*\\AppData\\Local\\CrashDumps\\*",
    };

    static std::vector<wstring> Recommended()
    {
        std::vector<wstring> v;
        for (const wchar_t* p : RecommendedExclusions) v.push_back(p);
        return v;
    }

    // one path per line; blank lines and the line ends removed
    static std::vector<wstring> Lines(const wstring& text, wchar_t sep)
    {
        std::vector<wstring> v;
        for (const wstring& l : Split(text, sep))
        {
            wstring t = Trim(l);
            if (!t.empty() && t != L"\r") v.push_back(Trim(t));
        }
        return v;
    }

    static wstring ThreadText(int threads)
    {
        return threads > 0 ? L::F(L"bk.threads_value", threads) : L::F(L"bk.threads_auto", (long long)App::Processors());
    }

    BackupTab::BackupTab(App& a, HWND p) : app(a), page(p), grid(p)
    {
        grid.labelWidth = 110;
        cboSource = Combo(page);
        btnFolder = Button(page, L::T(L"bk.btn_folder"));
        btnSrcRefresh = Button(page, L::T(L"adk.btn_refresh"));
        grid.Add(L::T(L"bk.row_source"), { Field(cboSource, 290), Btn(btnFolder, 110), Btn(btnSrcRefresh, 90) });
        txtWim = Edit(page);
        btnWim = Button(page, L::T(L"qbt.btn_browse"));
        grid.Add(L::T(L"bk.row_wim"), { Field(txtWim, 400), Btn(btnWim, 100) });
        txtName = Edit(page);
        lblDesc = Label(page, L::T(L"bk.row_desc"));
        txtDesc = Edit(page);
        grid.Add(L::T(L"bk.row_name"), { Field(txtName, 200), Txt(lblDesc, 8, 4), Field(txtDesc, 260) });
        cboComp = Combo(page);
        for (const wchar_t* k : { L"bk.comp_lzx", L"bk.comp_xpress", L"bk.comp_lzms" }) ComboAdd(cboComp, L::T(k));
        ComboSelect(cboComp, 0);
        lblThreads = Label(page, L::T(L"bk.row_threads"));
        numThreads = Number(page, 0, 64, 0);   // 0 = automatic (one per logical processor)
        grid.Add(L::T(L"bk.row_comp"), { Field(cboComp, 280), Txt(lblThreads, 8, 4), Field(numThreads, 60) });
        rbReplace = Radio(page, L::T(L"bk.existing_replace"), 1);
        rbAppend = Radio(page, L::T(L"bk.existing_append"), 1);
        SetChecked(rbReplace, true);
        // wimlib --update-of: the files unchanged since the last image are not read again
        chkUpdateOf = Check(page, L::T(L"bk.update_of"));
        SetChecked(chkUpdateOf, GetSetting(L"backup.update_of", L"1") == L"1");
        grid.Add(L::T(L"bk.row_existing"), { Box(rbReplace), Box(rbAppend), Box(chkUpdateOf) });
        // the user's exclusions (added to wimlib's own: pagefile.sys, hiberfil.sys, ...)
        chkExclude = Check(page, L::T(L"bk.exclude"));
        SetChecked(chkExclude, GetSetting(L"backup.exclude_on", L"0") == L"1");
        btnExclude = Button(page, L::T(L"bk.btn_exclude"));
        lblExclude = Label(page, L"", FontUI, color::Dim);
        // not saved yet: the recommended list
        wstring saved = GetSetting(L"backup.exclude", L"\x01");
        exclusions = saved == L"\x01" ? Recommended() : Lines(saved, L'|');
        grid.Add(L::T(L"bk.row_exclude"), { Box(chkExclude), Btn(btnExclude, 110), Txt(lblExclude, 4) });
        chkSnapshot = Check(page, L::T(L"bk.snapshot"));
        SetChecked(chkSnapshot, GetSetting(L"backup.snapshot", L"0") == L"1");
        grid.Add(L::T(L"bk.row_snapshot"), { Box(chkSnapshot) });
        chkVerify = Check(page, L::T(L"vf.after_backup"));
        grid.Add(L::T(L"vf.row"), { Box(chkVerify) });
        // wimlib --check: an integrity table in the WIM file (off by default)
        chkCheck = Check(page, L::T(L"bk.check"));
        grid.Add(L::T(L"bk.row_check"), { Box(chkCheck) });

        // below the rows: what the backup will do, then [Start backup]
        box = Panel(page, color::Panel);
        SetPaint(box, [](HDC dc, const RECT& r) { FrameRect(dc, &r, Brush(color::Line)); });
        title = Label(box, L::T(L"pv.title"), FontBold, color::Dim);
        const wchar_t* keyNames[] = { L"pv.source", L"pv.target", L"pv.method", L"pv.rights", L"pv.verify" };
        for (int i = 0; i < 5; i++)
        {
            keys[i] = Label(box, L::T(keyNames[i]), FontUI, color::Dim);
            // a long path breaks anywhere (as an edit control does)
            vals[i] = Label(box, L"", i == 1 ? FontMono : FontUI, color::Text, SS_LEFT | SS_EDITCONTROL);
        }
        btnCapture = Button(page, L::T(L"bk.btn_capture"));
        Primary(btnCapture, false);
        SetLayout(page, [this](int w, int h) { Layout(w, h); });
        SetLayout(box, [this](int w, int h) { LayoutBox(w, h); });

        OnClick(btnFolder, [this] { ChooseSourceFolder(); });
        OnClick(btnSrcRefresh, [this] { FillSources(); });
        OnClick(btnWim, [this] { ChooseWim(); });
        OnClick(btnCapture, [this] { StartCapture(); });
        OnClick(btnExclude, [this] { EditExclusions(); });
        for (HWND h : { cboSource, cboComp, txtWim, numThreads, rbReplace, rbAppend, chkVerify, chkCheck }) OnChange(h, [this] { UpdatePreview(); });
        OnChange(chkUpdateOf, [this]
        {
            SetSetting(L"backup.update_of", Checked(chkUpdateOf) ? L"1" : L"0");
            UpdatePreview();
        });
        OnChange(chkExclude, [this]
        {
            SetSetting(L"backup.exclude_on", Checked(chkExclude) ? L"1" : L"0");
            ShowExclusions();
            UpdatePreview();
        });
        // the running Windows drive is a source only with a snapshot
        OnChange(chkSnapshot, [this]
        {
            SetSetting(L"backup.snapshot", Checked(chkSnapshot) ? L"1" : L"0");
            FillSources();
            UpdatePreview();
        });
        app.busyHandlers.push_back([this](bool busy) { SetBusy(busy); });

        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t name[32];
        swprintf_s(name, L"backup_%04d-%02d-%02d", t.wYear, t.wMonth, t.wDay);
        SetText(txtName, name);
        FillSources();
        ShowExclusions();
        UpdatePreview();
    }

    void BackupTab::ShowExclusions()
    {
        SetText(lblExclude, L::F(L"bk.exclude_count", (long long)exclusions.size()));
        SetColor(lblExclude, Checked(chkExclude) ? color::Text : color::Dim);
        Relayout(page);
    }

    // the list in a dialog: one path per line, [Recommended] fills in the suggested one
    void BackupTab::EditExclusions()
    {
        HWND d = Dialog(L::T(L"bk.exclude_title"), 600, 420);
        HWND hint = Label(d, L::T(L"bk.exclude_hint"), FontUI, color::Dim, SS_LEFT);
        HWND t = Edit(d, ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL);
        SendMessageW(t, WM_SETFONT, (WPARAM)FontMono, TRUE);
        HWND def = Button(d, L::T(L"bk.exclude_recommended"));
        HWND ok = Button(d, L"OK"), cancel = Button(d, L::T(L"bk.btn_cancel"));
        Primary(ok, false);
        wstring text;
        for (const wstring& x : exclusions) text += x + L"\r\n";
        SetText(t, text);
        SetLayout(d, [=](int cw, int ch)
        {
            int x = S(14), w = std::max(S(100), cw - 2 * S(14));
            int hh = TextHeightWrapped(FontUI, GetText(hint), w);
            Move(hint, x, S(12), w, hh);
            int by = ch - S(14) - S(28);
            Move(t, x, S(12) + hh + S(8), w, std::max(S(40), by - S(10) - (S(12) + hh + S(8))));
            Move(def, x, by, S(150), S(28));
            Move(ok, cw - S(14) - S(86) * 2 - S(8), by, S(86), S(28));
            Move(cancel, cw - S(14) - S(86), by, S(86), S(28));
        });
        Relayout(d);
        OnClick(def, [t]
        {
            wstring r;
            for (const wstring& x : Recommended()) r += x + L"\r\n";
            SetText(t, r);
        });
        wstring typed;
        auto accept = [d, t, &typed] { typed = GetText(t); EndModal(d, 1); };
        OnClick(ok, accept);
        OnClick(cancel, [d] { EndModal(d, 2); });
        OnKeys(d, nullptr, [d] { EndModal(d, 2); });
        SetFocus(t);
        if (RunModal(d) != 1) return;
        std::vector<wstring> list;
        for (const wstring& l : Lines(typed, L'\n'))
        {
            // a path from the root of the source: "\..." (a name alone matches anywhere)
            if (l.find(L'|') != wstring::npos) continue;
            list.push_back(l);
        }
        exclusions = list;
        wstring joined;
        for (const wstring& x : exclusions) joined += (joined.empty() ? L"" : L"|") + x;
        SetSetting(L"backup.exclude", joined);
        if (!exclusions.empty() && !Checked(chkExclude))
        {
            SetChecked(chkExclude, true);
            SetSetting(L"backup.exclude_on", L"1");
        }
        ShowExclusions();
        UpdatePreview();
    }

    void BackupTab::Layout(int w, int)
    {
        // the box in the column of the fields, as high as its content; the button below it
        int y = grid.Place(0, 0, w);
        int x = S(grid.labelWidth), bw = std::max(0, std::min(w - x, S(640)));
        int bh = PlaceBox(bw, false);
        Move(box, x, y + S(6), bw, bh);
        Relayout(box);
        Move(btnCapture, x, y + S(6) + bh + S(12), S(160), S(28));
    }

    void BackupTab::LayoutBox(int w, int)
    {
        PlaceBox(w, true);
        // the lines move when a value gets longer: the whole box again
        RedrawWindow(box, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }

    int BackupTab::PlaceBox(int w, bool move)
    {
        int x = S(14), inner = std::max(0, w - 2 * S(14)), y = S(10);
        int th = TextHeight(FontBold);
        if (move) Move(title, x, y, inner, th);
        y += th + S(8);
        int kw = S(80), vw = std::max(0, inner - kw);
        for (int i = 0; i < 5; i++)
        {
            HFONT f = i == 1 ? FontMono : FontUI;
            int vh = TextHeightWrapped(f, GetText(vals[i]), vw);
            int kh = TextHeight(FontUI);
            if (move)
            {
                Move(keys[i], x, y, kw, kh);
                Move(vals[i], x + kw, y, vw, vh);
            }
            y += std::max(kh, vh) + (i < 4 ? S(5) : 0);
        }
        return y + S(10);
    }

    void BackupTab::Refresh()
    {
        if (Trim(GetText(txtWim)).empty()) SetText(txtWim, app.DefaultWim());
        FillSources();
    }

    void BackupTab::SetTarget(const wstring& wim)
    {
        SetText(txtWim, wim);
        SetChecked(rbAppend, true);
        UpdatePreview();
    }

    const Volume* BackupTab::Source() const
    {
        int i = ComboIndex(cboSource);
        return i >= 0 && i < (int)sources.size() ? &sources[i] : nullptr;
    }

    // the ready volumes except the Windows drive (allowed with a snapshot), plus a chosen folder
    void BackupTab::FillSources()
    {
        const Volume* cur = Source();
        wstring keep = cur ? cur->Path : wstring();
        std::vector<Volume> list;
        for (const Volume& v : sources) if (v.Letter == 0) list.push_back(v);
        for (Volume v : App::Volumes(true))
        {
            if (!IsWindowsDrive(v.Letter)) list.push_back(v);
            else if (Checked(chkSnapshot))
            {
                v.Text += L"  " + L::T(L"bk.vol_running");
                list.push_back(v);
            }
        }
        sources.clear();
        ComboClear(cboSource);
        sources = list;
        for (const Volume& v : sources) ComboAdd(cboSource, v.Text);
        for (size_t n = 0; n < sources.size(); n++)
            if (EqualsI(sources[n].Path, keep)) { ComboSelect(cboSource, (int)n); break; }
    }

    void BackupTab::ChooseSourceFolder()
    {
        wstring folder;
        if (!PickFolder(L::T(L"bk.select_source"), wstring(), folder)) return;
        Volume v;
        v.Path = folder;
        v.Text = L::F(L"bk.source_folder", folder);
        std::vector<Volume> list(1, v);
        for (const Volume& s : sources) if (s.Letter != 0) list.push_back(s);
        sources.clear();
        ComboClear(cboSource);
        sources = list;
        for (const Volume& s : sources) ComboAdd(cboSource, s.Text);
        ComboSelect(cboSource, 0);
    }

    void BackupTab::ChooseWim()
    {
        wstring cur = Trim(GetText(txtWim));
        if (cur.empty()) cur = app.DefaultWim();
        wstring full = FullPath(cur), file;
        if (!SaveFile({ { L"WIM (*.wim)", L"*.wim" } }, cur.empty() ? wstring() : DirName(full),
                cur.empty() ? wstring(L"backup.wim") : FileName(full), false, file)) return;
        SetText(txtWim, file);
    }

    void BackupTab::UpdatePreview()
    {
        const Volume* src = Source();
        SetText(vals[0], src ? src->Text : L"—");
        wstring wim = Trim(GetText(txtWim));
        SetText(vals[1], wim.empty() ? L"—" : wim);
        bool exists = !wim.empty() && FileExists(wim);
        bool append = exists && Checked(rbAppend);
        Enable(chkUpdateOf, Checked(rbAppend) && !app.Busy());
        wstring how = L::T(!exists ? L"pv.new" : append ? L"pv.append" : L"pv.replace");
        if (append && Checked(chkUpdateOf)) how += L" · " + L::T(L"pv.update_of");
        if (Excluding()) how += L" · " + L::F(L"pv.exclude", (long long)exclusions.size());
        int ci = std::max(0, std::min(2, ComboIndex(cboComp)));
        wstring comp = append ? L::T(L"bk.comp_existing") : wstring(CompNames[ci]);
        SetText(vals[2], how + L" · " + comp + L" · " + ThreadText(NumberValue(numThreads))
            + (Checked(chkSnapshot) ? L" · " + L::T(L"pv.snapshot") : wstring())
            + (Checked(chkCheck) ? L" · " + L::T(L"pv.check") : wstring()));
        SetText(vals[3], L::T(L"pv.admin"));
        SetText(vals[4], L::T(Checked(chkVerify) ? L"pv.on" : L"pv.off"));
        SetColor(vals[4], Checked(chkVerify) ? color::AccentSoft : color::Dim);
        // a longer value makes the box higher
        Relayout(page);
    }

    void BackupTab::SetBusy(bool busy)
    {
        for (HWND c : { btnCapture, cboSource, txtWim, chkVerify, chkCheck, chkSnapshot, chkExclude, btnExclude }) Enable(c, !busy);
        Enable(chkUpdateOf, !busy && Checked(rbAppend));
    }

    void BackupTab::StartCapture()
    {
        if (app.Busy()) return;
        const Volume* src = Source();
        wstring source = src ? src->Path : wstring();
        wstring wim = Trim(GetText(txtWim)), name = Trim(GetText(txtName));
        bool snapshot = Checked(chkSnapshot);
        wstring why = CaptureProblem(source, wim, snapshot);
        if (!why.empty()) { Info(L::F(why, source.empty() ? wstring(L"-") : source, wim), MB_ICONWARNING); return; }
        if (!app.LoadWimlib()) return;

        bool exists = FileExists(wim);
        bool append = exists && Checked(rbAppend);
        wstring what = L::T(L"bk.confirm_new");
        int newIndex = 1;
        if (append)
        {
            std::vector<wl::WimImage> old;
            if (!app.Images(wim, true, old)) return;
            for (const wl::WimImage& w : old)
                if (!name.empty() && EqualsI(w.Name, name)) { Info(L::F(L"bk.err_name_used", name), MB_ICONWARNING); return; }
            newIndex = (int)old.size() + 1;
            what = L::F(L"bk.confirm_append", newIndex);
        }
        else if (exists) what = L::T(L"bk.confirm_replace");
        if (snapshot) what += L"\r\n" + L::T(L"bk.confirm_snapshot");
        if (Checked(chkCheck)) what += L"\r\n" + L::T(L"bk.confirm_check");
        bool updateOf = append && Checked(chkUpdateOf) && newIndex > 1;
        if (updateOf) what += L"\r\n" + L::F(L"bk.confirm_update_of", newIndex - 1);
        if (Excluding()) what += L"\r\n" + L::F(L"bk.confirm_exclude", (long long)exclusions.size());
        int threads = NumberValue(numThreads);
        wstring threadText = ThreadText(threads);
        int ci = std::max(0, ComboIndex(cboComp));
        if (!YesNo(L::F(L"bk.confirm_capture", source, wim, name.empty() ? L::T(L"bk.no_name") : name,
                (append ? L::T(L"bk.comp_existing") : ComboItem(cboComp, ci)) + L", " + threadText, what), MB_ICONQUESTION))
            return;

        auto oneLine = [](wstring s) { for (wchar_t& c : s) if (c == L'\r' || c == L'\n') c = L' '; return s; };
        Job job;
        job[L"mode"] = L"capture";
        job[L"source"] = source;
        job[L"wim"] = wim;
        job[L"name"] = oneLine(name);
        job[L"description"] = oneLine(Trim(GetText(txtDesc)));
        job[L"compression"] = Num(CompTypes[std::min(2, ci)]);
        job[L"threads"] = Num(threads);
        job[L"append"] = append ? L"1" : L"0";
        job[L"verify"] = Checked(chkVerify) ? L"1" : L"0";
        job[L"check"] = Checked(chkCheck) ? L"1" : L"0";
        job[L"snapshot"] = snapshot ? L"1" : L"0";
        job[L"update_of"] = updateOf ? L"1" : L"0";
        if (Excluding())
        {
            job[L"exclude_count"] = Num((long long)exclusions.size());
            for (size_t i = 0; i < exclusions.size(); i++) job[L"x" + Num((long long)i + 1)] = exclusions[i];
        }
        app.job = App::JobInfo();
        app.job.verify = Checked(chkVerify);
        app.job.wim = wim;
        app.job.image = newIndex;
        app.onSuccess = [this](const wstring& elapsed) { return Done(elapsed); };
        app.onFailure = nullptr;
        app.Log(L::F(L"bk.log_start_capture", source, wim) + L"  (" + threadText + L")");
        app.StartHelper(job);
    }

    // the new image; the folder of the new WIM file is the default folder next time
    wstring BackupTab::Done(const wstring& elapsed)
    {
        const App::JobInfo& j = app.job;
        wstring msg = L::F(L"bk.capture_done", j.wim, j.image) + (j.verify ? L"\r\n" + L::T(L"vf.capture_verified") : wstring())
            + L"\r\n\r\n" + elapsed;
        wstring full = FullPath(j.wim);
        if (!full.empty()) SetSetting(L"image_folder", DirName(full));
        app.WimWritten(j.wim);
        UpdatePreview();
        return msg;
    }
}
