// ============================================================
//  7z_wimlib - settings.cpp
//  7z_wimlib.ini (see settings.h).
// ============================================================
#include "settings.h"

namespace sevenwim
{
    wstring ExeDir()
    {
        wchar_t b[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
        return DirName(wstring(b, n));
    }

    static wstring SettingsFile()
    {
        wstring f = Combine(ExeDir(), L"7z_wimlib.ini");
        wstring old = Combine(ExeDir(), L"Z_wimlib.ini");   // the name before the rename
        if (!FileExists(f) && FileExists(old)) MoveFileW(old.c_str(), f.c_str());
        return f;
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
}
