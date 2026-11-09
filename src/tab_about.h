// ============================================================
//  WimSmith - tab_about.h
//  The "?" tab: what the program is, its version, author, e-mail,
//  web page and license (the MIT text is embedded in the exe from
//  LICENSE, app.rc), and wimlib with its own license.
// ============================================================
#pragma once
#include "app.h"

namespace wimsmith
{
    struct AboutTab
    {
        AboutTab(App& app, HWND page);
        void Refresh();                       // the wimlib line (it may have been downloaded since)

        App& app;
        HWND page;

    private:
        HWND info, license;                   // the two group boxes
        HWND lblDesc, lnkMail, btnMail, lnkWeb, lblWimlib, lnkWimlib, txtLicense;
        ui::Grid grid;

        void Layout(int w, int h);
        void Open(const wstring& target);     // a link; without a browser (WinPE): copied
        void Copy(const wstring& text);
    };
}
