// ============================================================
//  7z_wimlib - tab_info.h
//  The info tab: the header of a WIM file and the details of its images
//  (read here, nothing is changed); an image can be taken over to the
//  restore, the export or the browse tab, the whole info copied, and
//  the file verified (helper, no administrator rights).
// ============================================================
#pragma once
#include "app.h"

namespace sevenwim
{
    struct InfoTab
    {
        InfoTab(App& app, HWND page);
        void Refresh();
        void ReadInfo(bool show);

        App& app;
        HWND page;
        HWND txtWim;

    private:
        HWND btnWim, btnRead, lblSummary, list, btnRestore, btnExport, btnBrowse, btnCopy, btnVerify;
        ui::Grid grid;
        wl::WimFileInfo info;
        bool hasInfo = false;
        wstring infoWim;

        void Layout(int w, int h);
        const wl::WimImage* Selected() const;
        void UpdateButtons();
        void ChooseWim();
        wstring Summary() const;
        wstring Details(const wl::WimImage& w) const;
        void Copy();
        void ToRestore();
        void ToExport();
        void ToBrowse();
        void StartVerify();
    };
}
