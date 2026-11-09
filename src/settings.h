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

    // relative_paths = 1 (Settings): image_folder is saved relative to the exe
    // folder when it is on the same drive, so a WimSmith on a USB drive finds it
    // after the drive letter changed. A relative image_folder is read as such
    // whatever the setting.
    bool RelativePaths();
    void SetRelativePaths(bool on);       // also writes image_folder again in the new form
    wstring ImageFolder();                // the folder of the last backup, full path ("" = none)
    void SetImageFolder(const wstring& dir);
}
