// ============================================================
//  WimSmith - mainwin.cpp
//  The main window and the log window (see mainwin.h).
// ============================================================
#include "mainwin.h"
#include "assoc.h"
#include "lang.h"
#include "settings.h"
#include "version.h"
#include <algorithm>

namespace wimsmith
{
    using namespace ui;

    // ---------- the log window ----------

    void LogWindow::Create(HWND owner)
    {
        win = TopWindow(L::T(L"lg.title"), 760, 420, owner);
        SetMinSize(win, 400, 240);
        box = Edit(win, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);
        SendMessageW(box, WM_SETFONT, (WPARAM)FontMono, FALSE);
        SendMessageW(box, EM_SETLIMITTEXT, 0, 0);
        HWND bar = Panel(win, color::Footer);
        HWND close = Button(bar, L::T(L"lg.btn_close"));
        HWND clear = Button(bar, L::T(L"lg.btn_clear"));
        HWND copy = Button(bar, L::T(L"lg.btn_copy"));
        SetLayout(win, [this, bar](int w, int h)
        {
            int barH = S(40);
            Move(box, 0, 0, w, h - barH);
            Move(bar, 0, h - barH, w, barH);
        });
        SetLayout(bar, [close, clear, copy](int w, int)
        {
            // right to left: [Close] [Clear] [Copy all]
            int x = w - S(6);
            x -= S(90); Move(close, x, S(6), S(90), S(28)); x -= S(8);
            x -= S(90); Move(clear, x, S(6), S(90), S(28)); x -= S(8);
            x -= S(110); Move(copy, x, S(6), S(110), S(28));
        });
        OnClick(close, [this] { ShowWindow(win, SW_HIDE); });
        OnClick(clear, [this] { SetWindowTextW(box, L""); });
        OnClick(copy, [this]
        {
            wstring text = GetText(box);
            if (text.empty() || !OpenClipboard(win)) return;
            EmptyClipboard();
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
            if (g)
            {
                memcpy(GlobalLock(g), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(g);
                if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
            }
            CloseClipboard();
        });
        // [X] hides the window; the lines stay
        OnClose(win, [this] { ShowWindow(win, SW_HIDE); return false; });
        Relayout(win);
    }

    void LogWindow::Append(const wstring& line)
    {
        int n = GetWindowTextLengthW(box);
        SendMessageW(box, EM_SETSEL, (WPARAM)n, (LPARAM)n);
        SendMessageW(box, EM_REPLACESEL, FALSE, (LPARAM)(line + L"\r\n").c_str());
    }

    // next to the main window (to its right when there is room, else over it)
    void LogWindow::ShowNear(HWND main)
    {
        if (!placed && main)
        {
            placed = true;
            RECT m, me;
            GetWindowRect(main, &m);
            GetWindowRect(win, &me);
            int w = me.right - me.left, h = me.bottom - me.top;
            MONITORINFO mi = { sizeof mi };
            GetMonitorInfoW(MonitorFromWindow(main, MONITOR_DEFAULTTONEAREST), &mi);
            RECT work = mi.rcWork;
            POINT p = { m.right + S(4), m.top };
            if (p.x + w > work.right) p = { m.left + (m.right - m.left - w) / 2, m.top + (m.bottom - m.top - h) / 2 };
            p.x = std::max(work.left, std::min(p.x, work.right - w));
            p.y = std::max(work.top, std::min(p.y, work.bottom - h));
            SetWindowPos(win, nullptr, p.x, p.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        if (IsIconic(win)) ShowWindow(win, SW_RESTORE);
        ShowWindow(win, SW_SHOW);
        SetForegroundWindow(win);
        int n = GetWindowTextLengthW(box);
        SendMessageW(box, EM_SETSEL, (WPARAM)n, (LPARAM)n);
        SendMessageW(box, EM_SCROLLCARET, 0, 0);
    }

    // ---------- the main window ----------

    void MainWindow::Create()
    {
        // a smaller screen (a Windows PE without a video driver): maximized, the pages scroll
        maximize = !FitsWorkArea(900, 600);
        hwnd = TopWindow(L"WimSmith " WS_VERSION_W, 900, 600, nullptr);
        // in the middle of the work area (C#: CenterScreen)
        RECT wr, work;
        GetWindowRect(hwnd, &wr);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        SetWindowPos(hwnd, nullptr, work.left + (work.right - work.left - (wr.right - wr.left)) / 2,
            work.top + (work.bottom - work.top - (wr.bottom - wr.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        Main = hwnd;
        SetMinSize(hwnd, 600, 400);
        log.Create(hwnd);

        tabs = ui::Tabs(hwnd);
        progress = Progress(hwnd);
        footer = Panel(hwnd, color::Footer);

        // footer: state | last log line | [Show log] | after done | [Cancel] | [Settings] | language
        lblState = Label(footer, L"", FontUI, color::Text, SS_LEFT | SS_CENTERIMAGE);
        lblLast = Link(footer, L"");
        SendMessageW(lblLast, WM_SETFONT, (WPARAM)FontMono, FALSE);
        SetWindowLongPtrW(lblLast, GWL_STYLE, GetWindowLongPtrW(lblLast, GWL_STYLE) | SS_ENDELLIPSIS | SS_CENTERIMAGE);
        SetColor(lblLast, color::Dim);
        Tooltip(lblLast, L::T(L"bk.note"));
        lnkLog = Link(footer, L::T(L"lg.btn_show"));
        SetWindowLongPtrW(lnkLog, GWL_STYLE, GetWindowLongPtrW(lnkLog, GWL_STYLE) | SS_CENTERIMAGE);
        lblAfter = Label(footer, L::T(L"bk.after_done"), FontUI, color::Dim, SS_LEFT | SS_CENTERIMAGE);
        cboAfter = Combo(footer);
        for (const wchar_t* k : { L"bk.after_nothing", L"bk.after_shutdown", L"bk.after_restart" }) ComboAdd(cboAfter, L::T(k));
        ComboSelect(cboAfter, 0);
        btnCancel = Button(footer, L::T(L"bk.btn_cancel"));
        Enable(btnCancel, false);
        btnSettings = Button(footer, L::T(L"st.btn"));
        cboLang = Combo(footer);
        for (const auto& kv : L::Available())
        {
            langCodes.push_back(kv.first);
            int i = ComboAdd(cboLang, kv.second);
            if (EqualsI(kv.first, L::Code)) SendMessageW(cboLang, CB_SETCURSEL, (WPARAM)i, 0);
        }

        OnClick(lblLast, [this] { log.ShowNear(hwnd); });
        OnClick(lnkLog, [this] { log.ShowNear(hwnd); });
        OnChange(cboLang, [this] { ChangeLanguage(); });
        OnClick(btnSettings, [this] { ShowSettings(); });
        OnChange(tabs, [this] { ShowPage(); });
        SetLayout(hwnd, [this](int w, int h) { Layout(w, h); });
        SetLayout(footer, [this](int w, int h) { LayoutFooter(w, h); });
        OnClose(hwnd, [this]
        {
            if (Busy() && !YesNo(L::T(L"z.close_busy"), MB_ICONWARNING)) return false;
            // an extraction of the browse tab: stopped with the window
            if (extracting && extracting())
            {
                if (!YesNo(L::T(L"z.close_extract"), MB_ICONWARNING)) return false;
                if (stopExtracting) stopExtracting();
            }
            return true;
        });
        OnDestroy(hwnd, [] { PostQuitMessage(0); });
        UpdateMenuLanguage();
    }

    HWND MainWindow::AddPage(const wstring& title)
    {
        // every page is laid out at least at the size the tabs were made for
        // (828 x 490); a smaller window scrolls it
        HWND page = nullptr;
        HWND host = ScrollHost(hwnd, 828, 490, page);
        pages.push_back(page);
        hosts.push_back(host);
        TabAdd(tabs, title);
        ShowWindow(host, SW_HIDE);
        if (pages.size() == 1) TabSelect(tabs, 0);
        return page;
    }

    void MainWindow::SelectPage(HWND page)
    {
        for (size_t i = 0; i < pages.size(); i++) if (pages[i] == page) TabSelect(tabs, (int)i);
    }

    bool MainWindow::PageShown(HWND page) const
    {
        int sel = TabIndex(tabs);
        return sel >= 0 && sel < (int)pages.size() && pages[sel] == page;
    }

    void MainWindow::ShowPage()
    {
        int sel = TabIndex(tabs);
        for (size_t i = 0; i < hosts.size(); i++) ShowWindow(hosts[i], (int)i == sel ? SW_SHOW : SW_HIDE);
    }

    void MainWindow::Layout(int w, int h)
    {
        int footH = S(38), progH = S(4);
        int pad = S(8), top = S(2);
        int head = TabHeight(tabs) + 1;
        Move(tabs, pad, top, w - 2 * pad, head);
        int pageTop = top + head + pad, pageBottom = h - footH - progH - pad;
        // a narrow window: the pages closer to the edge
        int side = w < S(860) ? pad : 2 * pad;
        for (HWND p : hosts) Move(p, side, pageTop, std::max(0, w - 2 * side), std::max(0, pageBottom - pageTop));
        InvalidateRect(tabs, nullptr, FALSE);
        Move(progress, 0, h - footH - progH, w, progH);
        Move(footer, 0, h - footH, w, footH);
    }

    void MainWindow::LayoutFooter(int w, int h)
    {
        auto mid = [h](int hh) { return (h - hh) / 2; };
        int textH = TextHeight(FontUI) + S(4);
        int x = S(12), right = w - S(8);
        // from the right: language, [Settings], [Cancel], combo, label, [Show log];
        // a narrow window (a small screen): narrower combos, no "When done" label
        bool narrow = w < S(820);
        int langW = narrow ? S(84) : S(100), afterW = narrow ? S(110) : S(130);
        int setW = std::max(narrow ? S(64) : S(80), TextWidth(FontUI, GetText(btnSettings)) + S(20));
        right -= langW; Move(cboLang, right, mid(S(23)), langW, S(23)); right -= S(4);
        right -= S(4) + setW; Move(btnSettings, right, mid(S(26)), setW, S(26)); right -= S(4);
        right -= S(8) + S(80); Move(btnCancel, right, mid(S(26)), S(80), S(26));
        right -= S(8) + afterW; Move(cboAfter, right, mid(S(23)), afterW, S(23));
        Show(lblAfter, !narrow);
        int aw = narrow ? 0 : TextWidth(FontUI, GetText(lblAfter)) + S(2);
        right -= S(4) + aw; Move(lblAfter, right, mid(textH), aw, textH);
        int lw = TextWidth(FontUI, GetText(lnkLog)) + S(2);
        right -= S(14) + lw; Move(lnkLog, right, mid(textH), lw, textH);
        right -= S(10);
        int sw = GetText(lblState).empty() ? 0 : TextWidth(FontUI, GetText(lblState)) + S(2);
        sw = std::min(sw, std::max(0, (right - x) / 2));
        Move(lblState, x, mid(textH), sw, textH);
        if (sw > 0) x += sw + S(12);
        Move(lblLast, x, mid(textH), std::max(0, right - x), textH);
    }

    void MainWindow::Log(const wstring& text)
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t time[16];
        swprintf_s(time, L"%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
        wstring line = wstring(time) + L"  " + text;
        log.Append(line);
        wstring one = line;
        for (size_t p; (p = one.find(L"\r\n")) != wstring::npos;) one.replace(p, 2, L"  ");
        SetText(lblLast, one);
    }

    void MainWindow::SetStatus(const wstring& text)
    {
        SetText(lblState, text);
        Relayout(footer);
    }

    void MainWindow::ChangeLanguage()
    {
        int i = ComboIndex(cboLang);
        if (i < 0 || i >= (int)langCodes.size() || EqualsI(langCodes[i], L::Code)) return;
        if (!CanRestart())
        {
            for (size_t n = 0; n < langCodes.size(); n++)
                if (EqualsI(langCodes[n], L::Code)) SendMessageW(cboLang, CB_SETCURSEL, n, 0);
            return;
        }
        SetSetting(L"language", langCodes[i]);
        restart = true;
        DestroyWindow(hwnd);
    }

    // the tabs a WIM file can open in (setting "open_tab"; App::OpenFile)
    static const wchar_t* const OpenTabs[][2] = {
        { L"browse", L"br.tab" }, { L"info", L"in.tab" }, { L"restore", L"bk.tab_apply" }, { L"append", L"st.menu_append" },
        { L"export", L"ex.tab" }, { L"splitjoin", L"sj.tab" } };
    // the type name of each extension in Explorer (assoc::Extensions)
    static const wchar_t* const AssocTypes[3] = { L"st.assoc_type_wim", L"st.assoc_type_esd", L"st.assoc_type_swm" };

    // the right-click menu of each extension, in the language shown: what
    // each kind of file can do (.esd: no new image, no split (solid);
    // .swm: no new image, join)
    static wstring RegisterMenus()
    {
        auto item = [](const wchar_t* id, const wchar_t* key) { return assoc::MenuItem{ id, L::T(key) }; };
        std::vector<assoc::MenuItem> wim = { item(L"restore", L"bk.tab_apply"), item(L"append", L"st.menu_append"),
            item(L"export", L"ex.tab"), item(L"splitjoin", L"sj.tab"), item(L"info", L"in.tab"), item(L"browse", L"br.tab") };
        std::vector<assoc::MenuItem> esd = { wim[0], wim[2], wim[4], wim[5] };
        std::vector<assoc::MenuItem> swm = { wim[0], wim[2], wim[3], wim[4], wim[5] };
        wstring err = assoc::RegisterMenu(L".wim", wim);
        if (err.empty()) err = assoc::RegisterMenu(L".esd", esd);
        if (err.empty()) err = assoc::RegisterMenu(L".swm", swm);
        assoc::Changed();
        return err;
    }

    // the menu made in another language: in this one (at every start, only when it differs)
    void MainWindow::UpdateMenuLanguage()
    {
        if (assoc::MenuRegistered() && assoc::MenuText(L".wim") != L::T(L"bk.tab_apply")) RegisterMenus();
    }

    // [Settings]: the theme (saved as theme = dark | light; the window is made
    // again, like for a language), .wim / .esd / .swm opened with WimSmith,
    // the tab they open in, and image_folder relative to the exe folder
    void MainWindow::ShowSettings()
    {
        HWND d = Dialog(L::T(L"st.title"), 600, 266);
        Grid grid(d);
        grid.labelWidth = 170;
        HWND cboTheme = Combo(d);
        ComboAdd(cboTheme, L::T(L"z.theme_dark"));
        ComboAdd(cboTheme, L::T(L"z.theme_light"));
        ComboSelect(cboTheme, DarkMode ? 0 : 1);
        grid.Add(L::T(L"st.row_theme"), { Field(cboTheme, 160) });
        const int extCount = 3;
        bool was[extCount], want[extCount];
        HWND chkExt[extCount];
        for (int n = 0; n < extCount; n++)
        {
            was[n] = want[n] = assoc::Registered(assoc::Extensions[n]);
            chkExt[n] = Check(d, assoc::Extensions[n]);
            SetChecked(chkExt[n], was[n]);
        }
        grid.Add(L::T(L"st.row_assoc"), { Box(chkExt[0]), Box(chkExt[1]), Box(chkExt[2]) });
        HWND cboTab = Combo(d);
        wstring tab = GetSetting(L"open_tab", L"browse");
        int tabIndex = 0;
        for (int n = 0; n < (int)(sizeof OpenTabs / sizeof OpenTabs[0]); n++)
        {
            ComboAdd(cboTab, L::T(OpenTabs[n][1]));
            if (EqualsI(tab, OpenTabs[n][0])) tabIndex = n;
        }
        ComboSelect(cboTab, tabIndex);
        grid.Add(L::T(L"st.row_open_tab"), { Field(cboTab, 220) });
        bool wasMenu = assoc::MenuRegistered(), wantMenu = wasMenu;
        HWND chkMenu = Check(d, L::T(L"st.menu"));
        SetChecked(chkMenu, wasMenu);
        grid.Add(L::T(L"st.row_menu"), { Box(chkMenu) });
        bool wasRelative = RelativePaths(), wantRelative = wasRelative;
        HWND chkRelative = Check(d, L::T(L"st.relative_paths"));
        SetChecked(chkRelative, wasRelative);
        grid.Add(L::T(L"st.row_paths"), { Box(chkRelative) });
        HWND ok = Button(d, L"OK"), cancel = Button(d, L::T(L"bk.btn_cancel"));
        Primary(ok, false);
        SetLayout(d, [=, &grid](int cw, int ch)
        {
            grid.Place(S(8), S(12), cw - S(16));
            int by = ch - S(14) - S(28);
            Move(ok, cw - S(14) - S(86) * 2 - S(8), by, S(86), S(28));
            Move(cancel, cw - S(14) - S(86), by, S(86), S(28));
        });
        Relayout(d);
        int theme = 0, openTab = 0;
        OnClick(ok, [&, d]
        {
            theme = ComboIndex(cboTheme);
            openTab = ComboIndex(cboTab);
            for (int n = 0; n < extCount; n++) want[n] = Checked(chkExt[n]);
            wantMenu = Checked(chkMenu);
            wantRelative = Checked(chkRelative);
            EndModal(d, 1);
        });
        OnClick(cancel, [d] { EndModal(d, 2); });
        OnKeys(d, nullptr, [d] { EndModal(d, 2); });
        if (RunModal(d) != 1) return;

        if (openTab >= 0) SetSetting(L"open_tab", OpenTabs[openTab][0]);
        if (wantRelative != wasRelative) SetRelativePaths(wantRelative);
        // each extension: set (again, when Windows does not open it with WimSmith) or taken back
        wstring done, removed, notDefault, failed;
        auto add = [](wstring& list, const wstring& ext) { list += (list.empty() ? L"" : L", ") + ext; };
        for (int n = 0; n < extCount; n++)
        {
            wstring ext = assoc::Extensions[n];
            if (want[n] && (!was[n] || !assoc::Default(ext)))
            {
                wstring err = assoc::Register(ext, L::T(AssocTypes[n]));
                if (!err.empty()) add(failed, ext + L" (" + err + L")");
                else if (!assoc::Default(ext)) add(notDefault, ext);
                else add(done, ext);
            }
            else if (!want[n] && was[n])
            {
                assoc::Unregister(ext);
                add(removed, ext);
            }
        }
        if (!done.empty() || !removed.empty() || !notDefault.empty() || !failed.empty()) assoc::Changed();
        if (!done.empty()) Log(L::F(L"st.assoc_done", done));
        if (!removed.empty()) Log(L::F(L"st.assoc_removed", removed));
        if (!failed.empty()) Info(L::F(L"st.assoc_failed", failed), MB_ICONWARNING);
        // another program chosen in Windows (UserChoice) stays: the user picks WimSmith once
        if (!notDefault.empty()) Info(L::F(L"st.assoc_not_default", notDefault));
        // the right-click menu: made again when on (this exe, this language), else taken away
        if (wantMenu)
        {
            wstring err = RegisterMenus();
            if (!err.empty()) Info(L::F(L"st.menu_failed", err), MB_ICONWARNING);
            else if (!wasMenu) Log(L::T(L"st.menu_done"));
        }
        else if (wasMenu)
        {
            for (const wchar_t* ext : assoc::Extensions) assoc::UnregisterMenu(ext);
            assoc::Changed();
            Log(L::T(L"st.menu_removed"));
        }
        if (theme >= 0 && (theme == 0) != DarkMode && CanRestart())
        {
            SetSetting(L"theme", theme == 0 ? L"dark" : L"light");
            restart = true;
            DestroyWindow(hwnd);
        }
    }

    bool MainWindow::CanRestart()
    {
        if (!Busy() && !(extracting && extracting())) return true;
        Info(L::T(L"z.lang_busy"), MB_ICONWARNING);
        return false;
    }
}
