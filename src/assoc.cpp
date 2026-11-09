// ============================================================
//  WimSmith - assoc.cpp
//  .wim -> WimSmith for the current user (see assoc.h).
// ============================================================
#include "assoc.h"
#include <shlobj.h>
#include <shlwapi.h>

namespace wimsmith
{
    namespace assoc
    {
        static const wchar_t* const ProgId = L"WimSmith.wim";
        static const wchar_t* const ProgKey = L"Software\\Classes\\WimSmith.wim";
        static const wchar_t* const ExtKey = L"Software\\Classes\\.wim";
        static const wchar_t* const Previous = L"WimSmith.PreviousDefault";   // the default of .wim before

        static wstring Self()
        {
            wchar_t b[MAX_PATH * 2];
            DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
            return wstring(b, n);
        }

        static wstring Command() { return L"\"" + Self() + L"\" \"%1\""; }

        // a REG_SZ of HKCU ("" = missing)
        static wstring Read(const wchar_t* key, const wchar_t* name)
        {
            wchar_t b[2048];
            DWORD size = sizeof b;
            if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, b, &size) != ERROR_SUCCESS) return L"";
            return b;
        }

        static LSTATUS Write(const wstring& key, const wchar_t* name, const wstring& value)
        {
            return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
        }

        bool Registered()
        {
            return EqualsI(Read((wstring(ProgKey) + L"\\shell\\open\\command").c_str(), nullptr), Command());
        }

        bool Default()
        {
            wchar_t exe[MAX_PATH * 2];
            DWORD n = MAX_PATH * 2;
            if (FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L".wim", L"open", exe, &n))) return false;
            return EqualsI(exe, Self());
        }

        wstring Register(const wstring& typeName)
        {
            wstring prog = ProgKey, ext = ExtKey;
            wstring before = Read(ExtKey, nullptr);
            LSTATUS r = Write(prog, nullptr, typeName);
            if (r == ERROR_SUCCESS) r = Write(prog + L"\\DefaultIcon", nullptr, L"\"" + Self() + L"\",0");
            if (r == ERROR_SUCCESS) r = Write(prog + L"\\shell\\open\\command", nullptr, Command());
            // the default of .wim before WimSmith (given back by Unregister)
            if (r == ERROR_SUCCESS && !before.empty() && !EqualsI(before, ProgId)) r = Write(prog, Previous, before);
            if (r == ERROR_SUCCESS) r = Write(ext, nullptr, ProgId);
            if (r == ERROR_SUCCESS) r = Write(ext + L"\\OpenWithProgids", ProgId, L"");
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            return r == ERROR_SUCCESS ? L"" : ErrorMessage((DWORD)r);
        }

        void Unregister()
        {
            wstring before = Read(ProgKey, Previous);
            if (EqualsI(Read(ExtKey, nullptr), ProgId))
            {
                if (before.empty()) RegDeleteKeyValueW(HKEY_CURRENT_USER, ExtKey, nullptr);
                else Write(ExtKey, nullptr, before);
            }
            RegDeleteKeyValueW(HKEY_CURRENT_USER, (wstring(ExtKey) + L"\\OpenWithProgids").c_str(), ProgId);
            RegDeleteTreeW(HKEY_CURRENT_USER, ProgKey);
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        }
    }
}
