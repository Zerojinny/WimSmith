// ============================================================
//  WimSmith - tab_about.cpp
//  The "?" tab (see tab_about.h).
// ============================================================
#include "tab_about.h"
#include "lang.h"
#include "libs.h"
#include "version.h"
#include <algorithm>
#include <shellapi.h>

namespace wimsmith
{
    using namespace ui;

    // the MIT text of LICENSE, embedded by app.rc (resource type TEXT), with CRLF line ends
    static wstring LicenseText()
    {
        HRSRC r = FindResourceW(nullptr, L"LICENSE", L"TEXT");
        HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
        const char* p = g ? (const char*)LockResource(g) : nullptr;
        if (!p) return WS_COPYRIGHT_W;
        wstring text = FromUtf8(std::string(p, SizeofResource(nullptr, r))), out;
        for (wchar_t c : text)
        {
            if (c == L'\r') continue;
            if (c == L'\n') out += L'\r';
            out += c;
        }
        return out;
    }

    AboutTab::AboutTab(App& a, HWND p) : app(a), page(p), grid(nullptr)
    {
        info = Group(page, L"WimSmith " WS_VERSION_W);
        grid.parent = info;
        lblDesc = Label(info, L::T(L"ab.desc"), FontUI, color::Text);
        grid.Add(L::T(L"ab.row_version"), { Txt(Label(info, WS_VERSION_W), 0) });
        grid.Add(L::T(L"ab.row_author"), { Txt(Label(info, WS_AUTHOR_W), 0) });
        lnkMail = Link(info, WS_EMAIL_W);
        btnMail = Button(info, L::T(L"ab.btn_copy"));
        grid.Add(L::T(L"ab.row_email"), { Txt(lnkMail, 0, 12), Btn(btnMail, 90) });
        lnkWeb = Link(info, WS_URL_W);
        grid.Add(L::T(L"ab.row_web"), { Txt(lnkWeb, 0) });
        grid.Add(L::T(L"ab.row_license"), { Txt(Label(info, L::T(L"ab.license")), 0) });
        lblWimlib = Label(info, L"");
        lnkWimlib = Link(info, L"https://wimlib.net");
        grid.Add(L"wimlib", { Txt(lblWimlib, 0, 12), Txt(lnkWimlib, 0) });

        license = Group(page, L::T(L"ab.group_license"));
        txtLicense = Edit(license, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);
        SendMessageW(txtLicense, WM_SETFONT, (WPARAM)FontMono, FALSE);
        SetText(txtLicense, LicenseText());

        SetLayout(page, [this](int w, int h) { Layout(w, h); });
        SetLayout(info, [this](int w, int)
        {
            int y = GroupTop() + S(6);
            Move(lblDesc, S(6), y, w - S(12), TextHeight(FontUI) + S(2));
            grid.Place(S(6), y + TextHeight(FontUI) + S(4), w - S(12));
        });
        SetLayout(license, [this](int w, int h)
        {
            Move(txtLicense, S(10), GroupTop() + S(4), std::max(0, w - S(20)), std::max(0, h - GroupTop() - S(14)));
        });

        OnClick(lnkMail, [this] { Open(L"mailto:" WS_EMAIL_W); });
        OnClick(btnMail, [this] { Copy(WS_EMAIL_W); });
        OnClick(lnkWeb, [this] { Open(WS_URL_W); });
        OnClick(lnkWimlib, [this] { Open(L"https://wimlib.net"); });
        Refresh();
    }

    void AboutTab::Layout(int w, int h)
    {
        // the program (description + rows), then the license text (the rest)
        int infoH = GroupTop() + S(6) + TextHeight(FontUI) + S(4) + (int)grid.rows.size() * S(grid.rowHeight) + S(10);
        Move(info, 0, 0, w, infoH);
        Move(license, 0, infoH + S(4), w, std::max(0, h - infoH - S(4)));
    }

    void AboutTab::Refresh()
    {
        wstring text;
        if (!libs::HasWimlib(app.root)) text = L::T(L"ab.wimlib_missing");
        else
        {
            wstring ver = libs::WimlibVersion(app.root);
            text = ver.empty() ? L::T(L"ab.wimlib_present") : L::F(L"ab.wimlib_version", ver);
        }
        SetText(lblWimlib, text);
        Relayout(info);
    }

    void AboutTab::Open(const wstring& target)
    {
        // a Windows PE has no browser or mail program: the address goes to the clipboard
        if ((INT_PTR)ShellExecuteW(app.win.hwnd, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32) return;
        Copy(target.compare(0, 7, L"mailto:") == 0 ? target.substr(7) : target);
    }

    void AboutTab::Copy(const wstring& text)
    {
        if (!OpenClipboard(app.win.hwnd)) { Info(ErrorMessage(GetLastError()), MB_ICONWARNING); return; }
        EmptyClipboard();
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (g)
        {
            memcpy(GlobalLock(g), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(g);
            if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
        }
        CloseClipboard();
        app.Status(L::F(L"ab.copied", text));
    }
}
