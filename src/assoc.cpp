// ============================================================
//  WimSmith - assoc.cpp
//  .wim / .esd / .swm -> WimSmith for the current user (see assoc.h).
// ============================================================
#include "assoc.h"
#include <shlobj.h>
#include <shlwapi.h>

namespace wimsmith
{
    namespace assoc
    {
        const wchar_t* const Extensions[3] = { L".wim", L".esd", L".swm" };
        static const wchar_t* const Previous = L"WimSmith.PreviousDefault";   // the default of the extension before

        static wstring ProgId(const wstring& ext) { return L"WimSmith" + ext; }      // WimSmith.wim
        static wstring ProgKey(const wstring& ext) { return L"Software\\Classes\\" + ProgId(ext); }
        static wstring ExtKey(const wstring& ext) { return L"Software\\Classes\\" + ext; }

        static wstring Self()
        {
            wchar_t b[MAX_PATH * 2];
            DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
            return wstring(b, n);
        }

        static wstring Command() { return L"\"" + Self() + L"\" \"%1\""; }

        // a REG_SZ of HKCU ("" = missing)
        static wstring Read(const wstring& key, const wchar_t* name)
        {
            wchar_t b[2048];
            DWORD size = sizeof b;
            if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, b, &size) != ERROR_SUCCESS) return L"";
            return b;
        }

        static LSTATUS Write(const wstring& key, const wchar_t* name, const wstring& value)
        {
            return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
        }

        bool Registered(const wstring& ext)
        {
            return EqualsI(Read(ProgKey(ext) + L"\\shell\\open\\command", nullptr), Command());
        }

        bool Default(const wstring& ext)
        {
            wchar_t exe[MAX_PATH * 2];
            DWORD n = MAX_PATH * 2;
            if (FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, ext.c_str(), L"open", exe, &n))) return false;
            return EqualsI(exe, Self());
        }

        wstring Register(const wstring& ext, const wstring& typeName)
        {
            wstring prog = ProgKey(ext), key = ExtKey(ext), id = ProgId(ext);
            wstring before = Read(key, nullptr);
            LSTATUS r = Write(prog, nullptr, typeName);
            if (r == ERROR_SUCCESS) r = Write(prog + L"\\DefaultIcon", nullptr, L"\"" + Self() + L"\",0");
            if (r == ERROR_SUCCESS) r = Write(prog + L"\\shell\\open\\command", nullptr, Command());
            // the default of the extension before WimSmith (given back by Unregister)
            if (r == ERROR_SUCCESS && !before.empty() && !EqualsI(before, id)) r = Write(prog, Previous, before);
            if (r == ERROR_SUCCESS) r = Write(key, nullptr, id);
            if (r == ERROR_SUCCESS) r = Write(key + L"\\OpenWithProgids", id.c_str(), L"");
            return r == ERROR_SUCCESS ? L"" : ErrorMessage((DWORD)r);
        }

        void Unregister(const wstring& ext)
        {
            wstring prog = ProgKey(ext), key = ExtKey(ext), id = ProgId(ext);
            wstring before = Read(prog, Previous);
            if (EqualsI(Read(key, nullptr), id))
            {
                if (before.empty()) RegDeleteKeyValueW(HKEY_CURRENT_USER, key.c_str(), nullptr);
                else Write(key, nullptr, before);
            }
            RegDeleteKeyValueW(HKEY_CURRENT_USER, (key + L"\\OpenWithProgids").c_str(), id.c_str());
            RegDeleteTreeW(HKEY_CURRENT_USER, prog.c_str());
        }

        void Changed() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }
    }
}
