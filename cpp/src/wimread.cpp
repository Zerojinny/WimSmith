// ============================================================
//  7z_wimlib (C++) - wimread.cpp
//  Reading a WIM file in the GUI process (no administrator rights):
//  the images for the restore / export lists.
// ============================================================
#include "wim.h"

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
    }
}
