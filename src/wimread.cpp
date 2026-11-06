// ============================================================
//  7z_wimlib - wimread.cpp
//  Reading a WIM file in the GUI process (no administrator rights):
//  the images for the restore / export lists, and the header with the
//  image details for the info tab.
// ============================================================
#include "wim.h"
#include <cstring>
#include <objbase.h>

namespace dvt
{
    namespace wl
    {
        int ReadImages(const wstring& wimFile, std::vector<WimImage>& list, int& total, int& found)
        {
            list.clear();
            Wim wim;
            int rc = OpenRead(wimFile, 0, nullptr, wim, total, found);
            if (rc != 0) return rc;
            WimInfo info;
            int count = info.Read(wim.p) ? info.ImageCount() : 0;
            for (int i = 1; i <= count; i++)
            {
                WimImage w;
                w.Index = i;
                w.Name = Property(wim.p, i, L"NAME");
                w.Description = Property(wim.p, i, L"DESCRIPTION");
                w.TotalBytes = ParseLong(Property(wim.p, i, L"TOTALBYTES"), -1);
                list.push_back(w);
            }
            return 0;
        }

        // the processor architecture of the image (PROCESSOR_ARCHITECTURE_*)
        static wstring ArchName(const wstring& value)
        {
            wstring v = Trim(value);
            if (v.empty()) return wstring();
            if (v == L"0") return L"x86";
            if (v == L"5") return L"ARM";
            if (v == L"6") return L"IA64";
            if (v == L"9") return L"x64";
            if (v == L"12") return L"ARM64";
            return v;
        }

        static bool ParseHex(wstring s, unsigned long long& v)
        {
            s = Trim(s);
            if (StartsWithI(s, L"0x")) s = s.substr(2);
            if (s.empty() || s.size() > 8) return false;
            v = 0;
            for (wchar_t c : s)
            {
                int d = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
                if (d < 0) return false;
                v = v * 16 + d;
            }
            return true;
        }

        // FILETIME of the XML data: HIGHPART / LOWPART as hex ("0x01DA...")
        static long long FileTime(const wstring& high, const wstring& low)
        {
            unsigned long long h, l;
            if (!ParseHex(high, h) || !ParseHex(low, l)) return 0;
            long long ft = (long long)((h << 32) | l);
            return ft > 0 ? ft : 0;
        }

        static WimImage Details(WIMStruct* wim, int i)
        {
            WimImage w;
            w.Index = i;
            w.Name = Property(wim, i, L"NAME");
            w.Description = Property(wim, i, L"DESCRIPTION");
            w.TotalBytes = ParseLong(Property(wim, i, L"TOTALBYTES"), -1);
            w.Flags = Property(wim, i, L"FLAGS");
            w.DisplayName = Property(wim, i, L"DISPLAYNAME");
            w.DirCount = ParseLong(Property(wim, i, L"DIRCOUNT"), -1);
            w.FileCount = ParseLong(Property(wim, i, L"FILECOUNT"), -1);
            w.Created = FileTime(Property(wim, i, L"CREATIONTIME/HIGHPART"), Property(wim, i, L"CREATIONTIME/LOWPART"));
            w.Modified = FileTime(Property(wim, i, L"LASTMODIFICATIONTIME/HIGHPART"), Property(wim, i, L"LASTMODIFICATIONTIME/LOWPART"));
            w.ProductName = Property(wim, i, L"WINDOWS/PRODUCTNAME");
            w.EditionId = Property(wim, i, L"WINDOWS/EDITIONID");
            w.InstallationType = Property(wim, i, L"WINDOWS/INSTALLATIONTYPE");
            w.Arch = ArchName(Property(wim, i, L"WINDOWS/ARCH"));
            wstring major = Property(wim, i, L"WINDOWS/VERSION/MAJOR");
            if (!major.empty())
            {
                w.Version = major + L"." + Property(wim, i, L"WINDOWS/VERSION/MINOR") + L"." + Property(wim, i, L"WINDOWS/VERSION/BUILD");
                wstring sp = Property(wim, i, L"WINDOWS/VERSION/SPBUILD");
                if (!sp.empty()) w.Version += L"." + sp;
            }
            w.DefaultLanguage = Property(wim, i, L"WINDOWS/LANGUAGES/DEFAULT");
            // repeated elements: LANGUAGE[1], LANGUAGE[2], ... (wimlib path syntax)
            for (int n = 1; n <= 64; n++)
            {
                wstring l = Property(wim, i, (L"WINDOWS/LANGUAGES/LANGUAGE[" + Num(n) + L"]").c_str());
                if (l.empty()) break;
                w.Languages += (w.Languages.empty() ? L"" : L", ") + l;
            }
            return w;
        }

        template <class T> static T At(const unsigned char* b, size_t off)
        {
            T v;
            memcpy(&v, b + off, sizeof v);
            return v;
        }

        int ReadInfo(const wstring& wimFile, WimFileInfo& f)
        {
            f = WimFileInfo();
            Wim wim;
            int rc = wimlib_open_wim_with_progress(wimFile.c_str(), 0, &wim.p, nullptr, nullptr);
            if (rc != 0) { wim.p = nullptr; return rc; }
            unsigned char b[256] = { 0 };
            rc = wimlib_get_wim_info(wim.p, b);
            if (rc != 0) return rc;
            // guid[16], image_count (16), boot_index (20), wim_version (24),
            // chunk_size (28), part_number (32, 16 bit), total_parts (34, 16 bit),
            // compression_type (36), total_bytes (40), flag bits (48)
            GUID g;
            memcpy(&g, b, sizeof g);
            wchar_t gs[64] = { 0 };
            StringFromGUID2(g, gs, 64);
            f.Guid = Lower(gs);
            f.ImageCount = At<int32_t>(b, 16);
            f.BootIndex = At<int32_t>(b, 20);
            f.Version = At<uint32_t>(b, 24);
            f.ChunkSize = At<int32_t>(b, 28);
            f.PartNumber = At<uint16_t>(b, 32);
            f.TotalParts = At<uint16_t>(b, 34);
            f.Compression = At<int32_t>(b, 36);
            f.TotalBytes = At<int64_t>(b, 40);
            uint32_t bits = At<uint32_t>(b, 48);
            f.Integrity = (bits & 0x1) != 0;
            f.ReadOnlyFlag = (bits & 0x10) != 0;
            f.Spanned = (bits & 0x20) != 0;
            f.Pipable = (bits & 0x200) != 0;
            f.FileBytes = FileSize(wimFile);
            for (int i = 1; i <= f.ImageCount; i++) f.Images.push_back(Details(wim.p, i));
            return 0;
        }
    }
}
