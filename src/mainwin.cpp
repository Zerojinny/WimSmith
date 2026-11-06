// ============================================================
//  7z_wimlib - mainwin.cpp
//  The main window and the log window (see mainwin.h).
// ============================================================
#include "mainwin.h"
#include "lang.h"
#include "settings.h"
#include <algorithm>

namespace dvt
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
        hwnd = TopWindow(L"7z_wimlib", 980, 540, nullptr);
        // in the middle of the work area (C#: CenterScreen)
        RECT wr, work;
        GetWindowRect(hwnd, &wr);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        SetWindowPos(hwnd, nullptr, work.left + (work.right - work.left - (wr.right - wr.left)) / 2,
            work.top + (work.bottom - work.top - (wr.bottom - wr.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        Main = hwnd;
        SetMinSize(hwnd, 820, 520);
        log.Create(hwnd);

        tabs = ui::Tabs(hwnd);
        progress = Progress(hwnd);
        footer = Panel(hwnd, color::Footer);

        // footer: state | last log line | [Show log] | after done | [Cancel] | language
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
        OnChange(tabs, [this] { ShowPage(); });
        SetLayout(hwnd, [this](int w, int h) { Layout(w, h); });
        SetLayout(footer, [this](int w, int h) { LayoutFooter(w, h); });
        OnClose(hwnd, [this]
        {
            if (Busy() && !YesNo(L::T(L"z.close_busy"), MB_ICONWARNING)) return false;
            // 7-Zip of the browse tab: stopped with the window
            if (extracting && extracting())
            {
                if (!YesNo(L::T(L"z.close_extract"), MB_ICONWARNING)) return false;
                if (stopExtracting) stopExtracting();
            }
            return true;
        });
        OnDestroy(hwnd, [] { PostQuitMessage(0); });
    }

    HWND MainWindow::AddPage(const wstring& title)
    {
        HWND page = Panel(hwnd, color::Back);
        pages.push_back(page);
        TabAdd(tabs, title);
        ShowWindow(page, SW_HIDE);
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
        for (size_t i = 0; i < pages.size(); i++) ShowWindow(pages[i], (int)i == sel ? SW_SHOW : SW_HIDE);
    }

    void MainWindow::Layout(int w, int h)
    {
        int footH = S(38), progH = S(4);
        int pad = S(8), top = S(2);
        int head = TabHeight(tabs) + 1;
        Move(tabs, pad, top, w - 2 * pad, head);
        int pageTop = top + head + pad, pageBottom = h - footH - progH - pad;
        for (HWND p : pages) Move(p, pad + pad, pageTop, w - 4 * pad, std::max(0, pageBottom - pageTop));
        Move(progress, 0, h - footH - progH, w, progH);
        Move(footer, 0, h - footH, w, footH);
    }

    void MainWindow::LayoutFooter(int w, int h)
    {
        auto mid = [h](int hh) { return (h - hh) / 2; };
        int textH = TextHeight(FontUI) + S(4);
        int x = S(12), right = w - S(8);
        // from the right: language, [Cancel], combo, label, [Show log]
        right -= S(100); Move(cboLang, right, mid(S(23)), S(100), S(23)); right -= S(4);
        right -= S(8) + S(80); Move(btnCancel, right, mid(S(26)), S(80), S(26));
        right -= S(8) + S(130); Move(cboAfter, right, mid(S(23)), S(130), S(23));
        int aw = TextWidth(FontUI, GetText(lblAfter)) + S(2);
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
        if (Busy() || (extracting && extracting()))
        {
            Info(L::T(L"z.lang_busy"), MB_ICONWARNING);
            for (size_t n = 0; n < langCodes.size(); n++)
                if (EqualsI(langCodes[n], L::Code)) SendMessageW(cboLang, CB_SETCURSEL, n, 0);
            return;
        }
        SetSetting(L"language", langCodes[i]);
        restart = true;
        DestroyWindow(hwnd);
    }
}
