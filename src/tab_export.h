// ============================================================
//  7z_wimlib - tab_export.h
//  The export tab: copies one image (or all) of a WIM / ESD file into
//  a new WIM file or into an existing one (other compression, ESD ->
//  WIM). Runs in the helper without administrator rights.
// ============================================================
#pragma once
#include "app.h"

namespace sevenwim
{
    struct ExportTab
    {
        ExportTab(App& app, HWND page);
        void Refresh();
        void ReadImages(bool show);
        void SetSource(const wstring& wim, int image);   // from the info tab

        App& app;
        HWND page;
        HWND txtSrc;

    private:
        HWND btnSrc, btnRead, cboImage, txtWim, btnWim, txtName, lblDesc, txtDesc;
        HWND cboComp, lblThreads, numThreads, lblThreadsHint, rbReplace, rbAppend, btnExport, hint;
        ui::Grid grid;
        std::vector<wl::WimImage> images;     // cboImage: item 0 = all images, then these

        void Layout(int w, int h);
        const wl::WimImage* Image() const;    // nullptr = all images (or none)
        void ChooseSource();
        void ChooseTarget();
        void SetBusy(bool busy);
        void StartExport();
    };
}
