// ============================================================
//  7z_wimlib - tab_info.h
//  The info tab: the header of a WIM file and the details of its images
//  (read here, nothing is changed); an image can be taken over to the
//  restore, the export or the browse tab, the whole info copied, and
//  the file verified (helper, no administrator rights).
//  [Manage]: name / description / edition of an image, the boot image,
//  delete an image, optimize the file (helper mode "modify"; elevated
//  only when the user may not write the file).
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
        HWND btnWim, btnRead, lblSummary, list, btnRestore, btnExport, btnBrowse, btnCopy, btnVerify, btnManage;
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
        wstring NotWritable() const;           // why the file cannot be changed ("" = it can)
        void ShowManageMenu();
        void EditImage(const wl::WimImage& w);
        void ToggleBoot(const wl::WimImage& w);
        void DeleteImage(const wl::WimImage& w);
        void Optimize();
        void StartModify(Job job, const wstring& doneText);
    };
}
