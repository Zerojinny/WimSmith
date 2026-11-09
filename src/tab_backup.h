// ============================================================
//  WimSmith - tab_backup.h
//  The backup tab: saves a volume or a folder as a WIM image (a new
//  file, or a new image of an existing file), optionally from a VSS
//  snapshot (then the running Windows drive may be saved) and verified
//  afterwards. Below the rows: what the backup will do (a box as high
//  as its content) and [Start backup]. The default file is
//  <folder of the last backup>\backup.wim.
//  A new image of an existing file can take the unchanged files from the
//  last image (wimlib --update-of: they are not read again); the user's
//  exclusions (WimSmith.ini: backup.exclude) go to the helper as x1..xn.
// ============================================================
#pragma once
#include "app.h"

namespace wimsmith
{
    struct BackupTab
    {
        BackupTab(App& app, HWND page);
        void Refresh();                       // the default file and the sources
        void SetTarget(const wstring& wim);   // a new image of this file (opened from Explorer)

        App& app;
        HWND page;

    private:
        HWND cboSource, btnFolder, btnSrcRefresh, txtWim, btnWim, txtName, lblDesc, txtDesc;
        HWND cboComp, lblThreads, numThreads, rbReplace, rbAppend, chkSnapshot, chkVerify, chkCheck;
        HWND chkUpdateOf, chkExclude, btnExclude, lblExclude;
        std::vector<wstring> exclusions;      // paths from the root of the source (wildcards * ?)
        HWND box, title, keys[5], vals[5], btnCapture;
        ui::Grid grid;
        std::vector<Volume> sources;          // the items of cboSource

        void Layout(int w, int h);
        void LayoutBox(int w, int h);
        int PlaceBox(int w, bool move);      // the content of the box; returns its height
        const Volume* Source() const;
        void FillSources();
        void ChooseSourceFolder();
        void ChooseWim();
        void UpdatePreview();
        void SetBusy(bool busy);
        void StartCapture();
        void EditExclusions();
        void ShowExclusions();
        bool Excluding() const { return ui::Checked(chkExclude) && !exclusions.empty(); }
        wstring Done(const wstring& elapsed);
    };
}
