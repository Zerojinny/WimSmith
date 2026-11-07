// ============================================================
//  7z_wimlib - settings.h
//  <exe folder>\7z_wimlib.ini, "key = value" lines (UTF-8), shared
//  with the C# version: language, image_folder, backup.*, restore.*,
//  disk.* (settings.cpp).
// ============================================================
#pragma once
#include "common.h"

namespace sevenwim
{
    wstring ExeDir();                     // the folder of this exe, without "\" at the end
    wstring GetSetting(const wstring& key, const wstring& fallback);
    void SetSetting(const wstring& key, const wstring& value);
    // several keys with one write; the other lines stay
    void SetSettings(const std::vector<std::pair<wstring, wstring>>& values);
}
