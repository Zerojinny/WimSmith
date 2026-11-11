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

        // the icon of the file type: icon resource 2 / 3 / 4 of the exe (app.rc)
        static wstring IconOf(const wstring& ext)
        {
            int id = EqualsI(ext, L".esd") ? 3 : EqualsI(ext, L".swm") ? 4 : 2;
            return L"\"" + Self() + L"\",-" + std::to_wstring(id);
        }

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
            if (r == ERROR_SUCCESS) r = Write(prog + L"\\DefaultIcon", nullptr, IconOf(ext));
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

        bool UpdateIcons()
        {
            bool changed = false;
            for (const wchar_t* ext : Extensions)
            {
                wstring key = ProgKey(ext) + L"\\DefaultIcon";
                if (Registered(ext) && !EqualsI(Read(key, nullptr), IconOf(ext)) && Write(key, nullptr, IconOf(ext)) == ERROR_SUCCESS) changed = true;
            }
            return changed;
        }

        void Changed() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }

        // ---------- the right-click menu ----------

        static wstring MenuKey(const wstring& ext) { return L"Software\\Classes\\SystemFileAssociations\\" + ext + L"\\shell\\WimSmith"; }
        static wstring MenuCommand(const wstring& id) { return L"\"" + Self() + L"\" --open " + id + L" \"%1\""; }

        bool MenuRegistered()
        {
            return EqualsI(Read(MenuKey(L".wim"), L"Icon"), L"\"" + Self() + L"\",0");
        }

        wstring MenuText(const wstring& ext)
        {
            wstring shell = MenuKey(ext) + L"\\shell";
            HKEY k;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, shell.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return L"";
            wchar_t name[256];
            DWORD n = 256;
            LSTATUS r = RegEnumKeyExW(k, 0, name, &n, nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(k);
            return r == ERROR_SUCCESS ? Read(shell + L"\\" + name, L"MUIVerb") : L"";
        }

        int MenuItemCount(const wstring& ext)
        {
            HKEY k;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, (MenuKey(ext) + L"\\shell").c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
            DWORD n = 0;
            RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, &n, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(k);
            return (int)n;
        }

        wstring RegisterMenu(const wstring& ext, const std::vector<MenuItem>& items)
        {
            wstring key = MenuKey(ext);
            RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());   // the items of before (another language, another list)
            LSTATUS r = Write(key, L"MUIVerb", L"WimSmith");
            if (r == ERROR_SUCCESS) r = Write(key, L"Icon", L"\"" + Self() + L"\",0");
            if (r == ERROR_SUCCESS) r = Write(key, L"SubCommands", L"");   // its items are under shell\ below it
            // the key names give the order: 1_..., 2_..., ...
            for (size_t n = 0; n < items.size() && r == ERROR_SUCCESS; n++)
            {
                wstring item = key + L"\\shell\\" + std::to_wstring(n + 1) + L"_" + items[n].id;
                r = Write(item, L"MUIVerb", items[n].text);
                if (r == ERROR_SUCCESS) r = Write(item + L"\\command", nullptr, MenuCommand(items[n].id));
            }
            return r == ERROR_SUCCESS ? L"" : ErrorMessage((DWORD)r);
        }

        void UnregisterMenu(const wstring& ext) { RegDeleteTreeW(HKEY_CURRENT_USER, MenuKey(ext).c_str()); }
    }
}
