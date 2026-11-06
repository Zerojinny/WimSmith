// ============================================================
//  7z_wimlib (C++) - tab_restore.h
//  The restore tab: formats a volume (NTFS) and applies one image of a
//  WIM file to it, after two confirmations, or applies it into an empty
//  folder; then (UEFI / BIOS) bcdboot writes the boot files to the
//  chosen system partition. Not offered: the Windows drive, the drive
//  of 7z_wimlib and the drive of the WIM file.
// ============================================================
#pragma once
#include "app.h"

namespace dvt
{
    struct RestoreTab
    {
        RestoreTab(App& app, HWND page);
        void Refresh();                       // the default file and the lists (App::RefreshStatus)
        void FillTargets();
        void ReadImages(bool show);
        void SetWim(const wstring& wim, int image);   // from the info tab / after a backup
        void SetLabel(const wstring& label);
        void SelectTarget(const wstring& path);       // a volume "X:\" (after the disk tab)

        App& app;
        HWND page;
        HWND txtWim, btnWim, btnRead, cboImage, cboTarget, btnTgtFolder, btnTgtRefresh, txtLabel;
        HWND chkUefi, chkBios, chkNoBoot, lblBootLang, cboBootLang, cboSysPart, btnPartRefresh, btnApply, hint;

    private:
        ui::Grid grid;
        std::vector<wl::WimImage> images;     // the items of cboImage
        std::vector<Volume> targets;          // the items of cboTarget
        struct SysItem { bool has = false; DiskPartition part; };
        std::vector<SysItem> sysItems;        // the items of cboSysPart
        std::vector<DiskPartition> partitions;
        bool partsKnown = false, partsLoading = false, bootSync = false;
        wstring bootTargetKey, bootFw;

        void Layout(int w, int h);
        const Volume* Target() const;
        const wl::WimImage* Image() const;
        wstring ImageText(int index) const;
        void LoadPartitions();
        const DiskPartition* TargetPartition() const;
        void BootBoxChanged(HWND box);
        wstring Firmware() const;
        void UpdateBoot();
        int DefaultSysPart(const DiskPartition& tp, const wstring& fw) const;
        void AddSysPart(const DiskPartition& p, const DiskPartition& target);
        void ChooseWim();
        void ChooseTargetFolder();
        void SetBusy(bool busy);
        void StartApply();
        void StartApplyToFolder(const wstring& wim, const wl::WimImage& img, const wstring& imgText, const wstring& folder);
    };
}
