// ============================================================
//  WimSmith - ramboot.h
//  "Boot once from this WIM" of the right-click menu of .wim files
//  (WimSmith.exe --open ramboot <file>): without the main window, only
//  its dialogs and message boxes. It reads the file (wimlib in this
//  process), lets the user choose an image, confirms, runs the elevated
//  helper (mode ramboot, helper.cpp RamBoot: boot index, boot.sdi, BCD
//  entry, bcdedit /bootsequence) with a small window while it works,
//  and asks to restart. With ramboot.delete_sdi = 1 the boot.sdi it
//  copied is deleted at the first logon after the PC has started again
//  (a Run value of the user: WimSmith.exe --ramboot-cleanup).
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    // the exit code of the process: 0 = done (or cancelled by the user)
    int RunOneTimeBoot(const wstring& wim);
    // "WimSmith.exe --ramboot-cleanup <sdi> <boot time>" (Run value): deletes the
    // boot.sdi once the PC has restarted since, and the Run value with it
    int RunSdiCleanup(const wstring& sdi, const wstring& bootTime);
    // [Settings]: delete the boot.sdi copied for it after the next start
    bool DeleteSdiAfterBoot();
    // shutdown.exe /r or /s now (no /f); "" = started, else the error
    wstring RunShutdown(bool restart);
}
