// ============================================================
//  7z_wimlib - tab_disk.h
//  The disk tab: clears a whole disk, initializes it as GPT or MBR and
//  creates the partitions of a layout:
//    Windows, GPT: EFI (FAT32) [+ MSR] + Windows (NTFS) [+ recovery]
//    Windows, MBR: system reserved (NTFS, active) + Windows [+ recovery]
//    Windows + data: the same, then a data partition; Windows gets a
//                  percentage of the free space, the data partition the rest
//    data        : one partition (NTFS / exFAT / FAT32) on the whole disk
//  The job runs in the elevated helper (mode = disk, disk.cpp). After a
//  Windows layout the restore tab gets the new partition as its target.
//  Every choice except the disk is kept in 7z_wimlib.ini (disk.*).
// ============================================================
#pragma once
#include "app.h"

namespace sevenwim
{
    struct DiskTab
    {
        DiskTab(App& app, HWND page);
        void LoadDisks();

        App& app;
        HWND page;

    private:
        HWND cboDisk, btnDiskRefresh, rbGpt, rbMbr, rbLayWin, rbLayWinData, rbLayData;
        HWND lblSysKind, numSys, lblSysMb, chkMsr, chkRec, numRec, lblRecMb, txtDiskLabel;
        HWND lblPct, numWinPct, lblPctSign, cboDataFs, lblLbl, txtDataLabel, lblPlan, btnGo;
        ui::Grid grid;
        struct DiskItem { bool has = false; DiskInfo disk; wstring problem; };
        std::vector<DiskItem> items;          // the items of cboDisk
        std::vector<DiskInfo> disks;
        bool disksKnown = false, disksRead = false, disksLoading = false, loaded = false;
        wstring settingsSaved;
        int jobDisk = -1;
        // the box after the system partition size: MSR on GPT, "create" (system reserved) on MBR
        bool msrOn = true, reservedOn = true, boxGpt = true;
        void SyncSysBox();
        DiskPlan jobPlan;

        void Layout(int w, int h);
        std::vector<std::pair<wstring, wstring>> Settings() const;
        static wstring SettingsText(const std::vector<std::pair<wstring, wstring>>& d);
        void LoadSettings();
        void SaveSettings();
        void FillDisks();
        void SetBusy(bool busy);
        DiskPlan CurrentPlan() const;
        const DiskInfo* SelectedDisk() const;
        static wstring MbText(long long bytes);
        void UpdateUi();
        wstring PlanText(const wstring& key, const DiskInfo& d, const DiskPlan& p) const;
        void StartDisk();
        wstring Done(const wstring& elapsed);
    };
}
