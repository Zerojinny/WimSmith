// ============================================================
//  WimSmith - settings.h
//  <exe folder>\WimSmith.ini, "key = value" lines (UTF-8), shared
//  with the C# version: language, image_folder, backup.*, restore.*,
//  disk.* (settings.cpp).
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    wstring ExeDir();                     // the folder of this exe, without "\" at the end
    wstring GetSetting(const wstring& key, const wstring& fallback);
    void SetSetting(const wstring& key, const wstring& value);
    // several keys with one write; the other lines stay
    void SetSettings(const std::vector<std::pair<wstring, wstring>>& values);

    // relative_paths = 1 (Settings): restore.wim, the WIM file of the restore tab,
    // is saved relative to the exe folder when it is on the same drive, so a
    // WimSmith on a USB drive finds it after the drive letter changed. A relative
    // restore.wim is read as such whatever the setting.
    bool RelativePaths();
    void SetRelativePaths(bool on);       // also writes restore.wim again in the new form
    wstring RestoreWim();                 // the last WIM file of the restore tab, full path ("" = none)
    void SetRestoreWim(const wstring& file);

    // DISM for the drivers of the restore tab (Settings): dism = system (the
    // dism.exe of Windows, System32) or folder (dism.exe in dism.folder, e.g.
    // the DISM folder of the ADK; a relative folder is read against the exe folder)
    bool DismFromFolder();
    wstring DismFolder();                 // as saved
    wstring DismExe();                    // the full path of the dism.exe to use
}
