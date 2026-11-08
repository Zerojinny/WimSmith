// ============================================================
//  7z_wimlib - wim.cpp
//  Loading libwim-15.dll (see wim.h).
// ============================================================
#include "wim.h"
#include <cstring>

namespace sevenwim
{
    namespace wl
    {
#define WL_DEFINE(ret, name, params) ret(__cdecl* name) params = nullptr;
        WL_FUNCS(WL_DEFINE)
#undef WL_DEFINE

        static bool loaded;

        wstring Load(const wstring& toolDir, bool acquirePrivileges)
        {
            if (loaded) return wstring();
#ifndef _WIN64
            return wstring(DllName) + L" needs a 64-bit process.";
#else
            wstring dll = Combine(toolDir, DllName);
            if (!FileExists(dll)) return L"Not found: " + dll;
            HMODULE h = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (!h) return L"LoadLibrary " + dll + L": error " + Num(GetLastError());
#define WL_LOAD(ret, name, params) \
            name = reinterpret_cast<ret(__cdecl*) params>(GetProcAddress(h, #name)); \
            if (!name) return dll + L": " + FromUtf8(#name) + L" is missing (wimlib 1.14 or later is needed).";
            WL_FUNCS(WL_LOAD)
#undef WL_LOAD
            int rc = wimlib_global_init(acquirePrivileges ? 0 : InitDontAcquirePrivileges);
            if (rc != 0) return ErrorText(rc);
            loaded = true;
            return wstring();
#endif
        }

        wstring ErrorText(int code)
        {
            const wchar_t* s = wimlib_get_error_string ? wimlib_get_error_string(code) : nullptr;
            return L"wimlib error " + Num(code) + (s && *s ? L": " + wstring(s) : wstring());
        }

        wstring Property(const WIMStruct* wim, int image, const wchar_t* name)
        {
            const wchar_t* p = wimlib_get_image_property(wim, image, name);
            return p ? wstring(p) : wstring();
        }

        template <class T> static T At(const unsigned char* b, size_t off)
        {
            T v;
            memcpy(&v, b + off, sizeof v);
            return v;
        }

        bool WimInfo::Read(WIMStruct* wim)
        {
            memset(b, 0, sizeof b);
            return wimlib_get_wim_info(wim, b) == 0;
        }
        int WimInfo::ImageCount() const { return At<int32_t>(b, 16); }
        int WimInfo::BootIndex() const { return At<int32_t>(b, 20); }
        int WimInfo::PartNumber() const { return At<uint16_t>(b, 32); }
        int WimInfo::TotalParts() const { return At<uint16_t>(b, 34); }
        bool WimInfo::Integrity() const { return (At<uint32_t>(b, 48) & 0x1) != 0; }
    }
}
