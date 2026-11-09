// ============================================================
//  WimSmith - settings.cpp
//  WimSmith.ini (see settings.h).
// ============================================================
#include "settings.h"
#include <shlwapi.h>

namespace wimsmith
{
    wstring ExeDir()
    {
        wchar_t b[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
        return DirName(wstring(b, n));
    }

    static wstring SettingsFile()
    {
        return Combine(ExeDir(), L"WimSmith.ini");
    }

    static std::vector<wstring> ReadLines()
    {
        std::vector<wstring> lines;
        wstring f = SettingsFile();
        if (!FileExists(f)) return lines;
        try { lines = SplitLines(ReadTextFile(f)); }
        catch (const HelperError&) {}
        if (!lines.empty() && lines.back().empty()) lines.pop_back();
        return lines;
    }

    static bool KeyOf(const wstring& line, const wstring& key)
    {
        wstring t = Trim(line);
        size_t eq = t.find(L'=');
        return !t.empty() && t[0] != L';' && eq != wstring::npos && eq > 0 && EqualsI(Trim(t.substr(0, eq)), key);
    }

    wstring GetSetting(const wstring& key, const wstring& fallback)
    {
        for (const wstring& l : ReadLines())
        {
            wstring t = Trim(l);
            if (!t.empty() && t[0] == L'#') continue;
            if (KeyOf(t, key))
            {
                wstring v = Trim(t.substr(t.find(L'=') + 1));
                return v.empty() ? fallback : v;
            }
        }
        return fallback;
    }

    void SetSetting(const wstring& key, const wstring& value) { SetSettings({ std::make_pair(key, value) }); }

    void SetSettings(const std::vector<std::pair<wstring, wstring>>& values)
    {
        std::vector<wstring> lines = ReadLines();
        for (const auto& kv : values)
        {
            bool done = false;
            for (wstring& l : lines)
                if (KeyOf(l, kv.first)) { l = kv.first + L" = " + kv.second; done = true; }
            if (!done) lines.push_back(kv.first + L" = " + kv.second);
        }
        wstring text;
        for (const wstring& l : lines) text += l + L"\r\n";
        WriteBytes(SettingsFile(), ToUtf8(text));
    }

    bool RelativePaths() { return GetSetting(L"relative_paths", L"0") == L"1"; }

    // dir relative to the exe folder (".\WIM" -> "WIM", "..\Backup"), or "" when
    // that is not possible (another drive, a network path)
    static wstring RelativeToExe(const wstring& dir)
    {
        wchar_t rel[MAX_PATH];
        if (dir.size() >= MAX_PATH || !PathRelativePathToW(rel, ExeDir().c_str(), FILE_ATTRIBUTE_DIRECTORY, dir.c_str(), FILE_ATTRIBUTE_DIRECTORY))
            return wstring();
        wstring r = rel;
        if (r.compare(0, 2, L".\\") == 0) r = r.substr(2);
        return r.empty() ? wstring(L".") : r;
    }

    wstring ImageFolder()
    {
        wstring dir = GetSetting(L"image_folder", L"");
        if (dir.empty() || !PathIsRelativeW(dir.c_str())) return dir;
        wstring full = FullPath(Combine(ExeDir(), dir));
        while (full.size() > 3 && full.back() == L'\\') full.pop_back();
        return full;
    }

    void SetImageFolder(const wstring& dir)
    {
        wstring value = dir;
        if (RelativePaths())
        {
            wstring rel = RelativeToExe(dir);
            if (!rel.empty()) value = rel;
        }
        SetSetting(L"image_folder", value);
    }

    void SetRelativePaths(bool on)
    {
        wstring dir = ImageFolder();
        SetSetting(L"relative_paths", on ? L"1" : L"0");
        if (!dir.empty()) SetImageFolder(dir);
    }
}
