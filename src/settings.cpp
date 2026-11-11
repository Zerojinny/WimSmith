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

    // file relative to the exe folder (".\WIM\a.wim" -> "WIM\a.wim", "..\Backup\a.wim"),
    // or "" when that is not possible (another drive, a network path)
    static wstring RelativeToExe(const wstring& file)
    {
        wchar_t rel[MAX_PATH];
        if (file.size() >= MAX_PATH || !PathRelativePathToW(rel, ExeDir().c_str(), FILE_ATTRIBUTE_DIRECTORY, file.c_str(), FILE_ATTRIBUTE_NORMAL))
            return wstring();
        wstring r = rel;
        if (r.compare(0, 2, L".\\") == 0) r = r.substr(2);
        return r;
    }

    wstring RestoreWim()
    {
        wstring file = GetSetting(L"restore.wim", L"");
        if (file.empty() || !PathIsRelativeW(file.c_str())) return file;
        return FullPath(Combine(ExeDir(), file));
    }

    void SetRestoreWim(const wstring& file)
    {
        wstring value = file;
        if (RelativePaths())
        {
            wstring rel = RelativeToExe(file);
            if (!rel.empty()) value = rel;
        }
        if (value != GetSetting(L"restore.wim", L"")) SetSetting(L"restore.wim", value);
    }

    void SetRelativePaths(bool on)
    {
        wstring file = RestoreWim();
        SetSetting(L"relative_paths", on ? L"1" : L"0");
        if (!file.empty()) SetRestoreWim(file);
    }

    bool DismFromFolder() { return EqualsI(GetSetting(L"dism", L"system"), L"folder"); }
    wstring DismFolder() { return GetSetting(L"dism.folder", L""); }

    int AfterSeconds()
    {
        long long v;
        if (!TryParseLong(GetSetting(L"after_seconds", L""), v)) return AfterSecondsDefault;
        return (int)std::max<long long>(1, std::min<long long>(AfterSecondsMax, v));
    }

    wstring DismExe()
    {
        if (!DismFromFolder()) return Combine(SystemDir(), L"dism.exe");
        wstring dir = DismFolder();
        if (dir.empty()) return wstring();
        if (PathIsRelativeW(dir.c_str())) dir = FullPath(Combine(ExeDir(), dir));
        return Combine(dir, L"dism.exe");
    }

    static void CopyOne(const wstring& from, const wstring& to, int& count)
    {
        if (!CopyFileW(from.c_str(), to.c_str(), FALSE))
            throw HelperError{ from + L": " + ErrorMessage(GetLastError()) };
        count++;
    }

    // all files of a folder and its subfolders
    static void CopyTree(const wstring& from, const wstring& to, int& count)
    {
        CreateDirs(to);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(Combine(from, L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do
        {
            wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) CopyTree(Combine(from, name), Combine(to, name), count);
            else CopyOne(Combine(from, name), Combine(to, name), count);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    int ImportSystemDism(const wstring& dir)
    {
        wstring sys = SystemDir(), dismDir = Combine(sys, L"Dism");
        if (!FileExists(Combine(sys, L"dism.exe")) || !DirExists(dismDir))
            throw HelperError{ Combine(sys, L"dism.exe") + L", " + dismDir + L": " + ErrorMessage(ERROR_FILE_NOT_FOUND) };
        int count = 0;
        // System32\Dism\* (DismCore, providers, DismHost, <lang>\*.mui) flat into
        // the folder, like the DISM folder of the ADK
        CopyTree(dismDir, dir, count);
        // dism.exe and the DLLs it loads from System32, with their .mui files
        const wchar_t* files[] = { L"dism.exe", L"dismapi.dll", L"wimgapi.dll" };
        for (const wchar_t* f : files)
            if (FileExists(Combine(sys, f))) CopyOne(Combine(sys, f), Combine(dir, f), count);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(Combine(dismDir, L"*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                wstring lang = fd.cFileName;
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || lang == L"." || lang == L"..") continue;
                for (const wchar_t* f : files)
                {
                    wstring mui = Combine(Combine(sys, lang), wstring(f) + L".mui");
                    if (FileExists(mui)) CopyOne(mui, Combine(Combine(dir, lang), wstring(f) + L".mui"), count);
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        return count;
    }
}
