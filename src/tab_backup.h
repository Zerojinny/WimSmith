// ============================================================
//  7z_wimlib - tab_backup.h
//  The backup tab: saves a volume or a folder as a WIM image (a new
//  file, or a new image of an existing file), optionally from a VSS
//  snapshot (then the running Windows drive may be saved) and verified
//  afterwards. At the right: what the backup will do, the hint and
//  [Start backup]. The default file is <folder of the last backup>\backup.wim.
// ============================================================
#pragma once
#include "app.h"

namespace sevenwim
{
    struct BackupTab
    {
        BackupTab(App& app, HWND page);
        void Refresh();                       // the default file and the sources

        App& app;
        HWND page;

    private:
        HWND cboSource, btnFolder, btnSrcRefresh, txtWim, btnWim, txtName, lblDesc, txtDesc;
        HWND cboComp, lblThreads, numThreads, rbReplace, rbAppend, chkSnapshot, chkVerify, chkCheck;
        HWND box, title, keys[5], vals[5], hint, btnCapture;
        ui::Grid grid;
        std::vector<Volume> sources;          // the items of cboSource

        void Layout(int w, int h);
        void LayoutBox(int w, int h);
        const Volume* Source() const;
        void FillSources();
        void ChooseSourceFolder();
        void ChooseWim();
        void UpdatePreview();
        void SetBusy(bool busy);
        void StartCapture();
        wstring Done(const wstring& elapsed);
    };
}
