// ============================================================
//  7z_wimlib - storage.cpp
//  Disks, partitions and volumes with the Windows API (see storage.h).
// ============================================================
#include "storage.h"
#include <winioctl.h>
#include <objbase.h>
#include <algorithm>

namespace dvt
{
    namespace storage
    {
        // ---------- handles and IOCTLs ----------

        struct Handle
        {
            HANDLE h = INVALID_HANDLE_VALUE;
            Handle() {}
            explicit Handle(HANDLE x) : h(x) {}
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            Handle(Handle&& o) noexcept : h(o.h) { o.h = INVALID_HANDLE_VALUE; }
            ~Handle() { if (ok()) CloseHandle(h); }
            bool ok() const { return h != INVALID_HANDLE_VALUE && h != nullptr; }
        };

        // access 0: only queries (no administrator rights needed)
        static HANDLE OpenDevice(const wstring& path, DWORD access)
        {
            return CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        }

        static wstring DiskPath(int n) { return L"\\\\.\\PhysicalDrive" + Num(n); }

        static bool Ioctl(HANDLE h, DWORD code, const void* in, DWORD inSize, void* out, DWORD outSize, DWORD* got = nullptr)
        {
            DWORD n = 0;
            BOOL ok = DeviceIoControl(h, code, const_cast<void*>(in), inSize, out, outSize, &n, nullptr);
            if (got) *got = n;
            return ok != FALSE;
        }

        // an output that grows (layouts, extents)
        static bool IoctlGrow(HANDLE h, DWORD code, std::vector<BYTE>& buf, size_t start)
        {
            buf.assign(start, 0);
            for (int n = 0; n < 8; n++)
            {
                if (Ioctl(h, code, nullptr, 0, buf.data(), (DWORD)buf.size())) return true;
                DWORD e = GetLastError();
                if (e != ERROR_INSUFFICIENT_BUFFER && e != ERROR_MORE_DATA) return false;
                buf.assign(buf.size() * 2, 0);
            }
            return false;
        }

        static wstring GuidText(const GUID& g)
        {
            wchar_t b[64] = {};
            StringFromGUID2(g, b, 64);
            return Lower(b);
        }

        // ---------- volumes ----------

        static std::vector<Extent> ExtentsOf(HANDLE h)
        {
            std::vector<Extent> r;
            std::vector<BYTE> buf;
            if (!IoctlGrow(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, buf, sizeof(VOLUME_DISK_EXTENTS) + 8 * sizeof(DISK_EXTENT)))
                return r;
            const VOLUME_DISK_EXTENTS* v = (const VOLUME_DISK_EXTENTS*)buf.data();
            for (DWORD n = 0; n < v->NumberOfDiskExtents; n++)
            {
                Extent e;
                e.Disk = (int)v->Extents[n].DiskNumber;
                e.Offset = v->Extents[n].StartingOffset.QuadPart;
                e.Length = v->Extents[n].ExtentLength.QuadPart;
                r.push_back(e);
            }
            return r;
        }

        // \\?\Volume{guid}\ -> \\?\Volume{guid} (the device, not its root folder)
        static wstring NoSlash(const wstring& p)
        {
            return !p.empty() && p.back() == L'\\' ? p.substr(0, p.size() - 1) : p;
        }

        std::vector<Volume> Volumes()
        {
            std::vector<Volume> r;
            wchar_t name[MAX_PATH] = {};
            HANDLE f = FindFirstVolumeW(name, MAX_PATH);
            if (f == INVALID_HANDLE_VALUE) return r;
            do
            {
                Volume v;
                v.Path = name;
                {
                    Handle h(OpenDevice(NoSlash(v.Path), 0));
                    if (h.ok()) v.Extents = ExtentsOf(h.h);
                }
                std::vector<wchar_t> paths(1024);
                DWORD need = 0;
                if (!GetVolumePathNamesForVolumeNameW(v.Path.c_str(), paths.data(), (DWORD)paths.size(), &need)
                    && GetLastError() == ERROR_MORE_DATA)
                {
                    paths.assign(need + 1, 0);
                    if (!GetVolumePathNamesForVolumeNameW(v.Path.c_str(), paths.data(), (DWORD)paths.size(), &need)) paths.assign(2, 0);
                }
                for (const wchar_t* p = paths.data(); *p; p += wcslen(p) + 1)
                    if (wcslen(p) == 3 && p[1] == L':' && IsCharAlphaW(p[0])) { v.Letter = UpperChar(p[0]); break; }
                wchar_t fs[MAX_PATH + 1] = {};
                if (GetVolumeInformationW(v.Path.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fs, MAX_PATH + 1))
                    v.FileSystem = fs;
                r.push_back(v);
            } while (FindNextVolumeW(f, name, MAX_PATH));
            FindVolumeClose(f);
            return r;
        }

        // ---------- disks ----------

        // the names Get-Disk uses for STORAGE_BUS_TYPE
        static wstring BusName(int t)
        {
            static const wchar_t* const names[] = {
                L"Unknown", L"SCSI", L"ATAPI", L"ATA", L"1394", L"SSA", L"Fibre Channel", L"USB", L"RAID", L"iSCSI",
                L"SAS", L"SATA", L"SD", L"MMC", L"Virtual", L"File Backed Virtual", L"Storage Spaces", L"NVMe", L"SCM", L"UFS" };
            return t >= 0 && t < (int)(sizeof(names) / sizeof(names[0])) ? names[t] : L"Unknown";
        }

        static wstring AsciiAt(const std::vector<BYTE>& buf, DWORD offset)
        {
            if (offset == 0 || offset >= buf.size()) return wstring();
            wstring s;
            for (size_t n = offset; n < buf.size() && buf[n]; n++) s += (wchar_t)buf[n];
            return Trim(s);
        }

        // the model as Windows shows it: "Msft Virtual Disk", "Samsung SSD 850 EVO 250GB"
        static void ReadDevice(HANDLE h, Disk& d)
        {
            STORAGE_PROPERTY_QUERY q = {};
            q.PropertyId = StorageDeviceProperty;
            q.QueryType = PropertyStandardQuery;
            std::vector<BYTE> buf(4096, 0);
            if (!Ioctl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf.data(), (DWORD)buf.size())) return;
            const STORAGE_DEVICE_DESCRIPTOR* s = (const STORAGE_DEVICE_DESCRIPTOR*)buf.data();
            d.Bus = BusName((int)s->BusType);
            wstring vendor = AsciiAt(buf, s->VendorIdOffset), product = AsciiAt(buf, s->ProductIdOffset);
            // ATA / NVMe disks carry the whole model in the product id
            if (EqualsI(vendor, L"ATA") || EqualsI(vendor, L"NVMe") || StartsWithI(product, vendor)) vendor.clear();
            d.Name = vendor.empty() ? product : product.empty() ? vendor : vendor + L" " + product;
        }

        static bool ReadLayout(HANDLE h, std::vector<BYTE>& buf)
        {
            return IoctlGrow(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, buf,
                sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 16 * sizeof(PARTITION_INFORMATION_EX));
        }

        static bool ReadDisk(int n, Disk& d)
        {
            Handle h(OpenDevice(DiskPath(n), 0));
            if (!h.ok()) return false;
            d.Number = n;
            BYTE geo[sizeof(DISK_GEOMETRY_EX) + 256] = {};
            if (!Ioctl(h.h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, geo, sizeof(geo))) return false;
            d.Size = ((const DISK_GEOMETRY_EX*)geo)->DiskSize.QuadPart;
            ReadDevice(h.h, d);

            GET_DISK_ATTRIBUTES attr = {};
            attr.Version = sizeof(attr);
            if (Ioctl(h.h, IOCTL_DISK_GET_DISK_ATTRIBUTES, nullptr, 0, &attr, sizeof(attr)))
            {
                d.Offline = (attr.Attributes & DISK_ATTRIBUTE_OFFLINE) != 0;
                d.ReadOnly = (attr.Attributes & DISK_ATTRIBUTE_READ_ONLY) != 0;
            }
            // a write-protected medium (the switch of an SD card)
            if (!Ioctl(h.h, IOCTL_DISK_IS_WRITABLE, nullptr, 0, nullptr, 0) && GetLastError() == ERROR_WRITE_PROTECT)
                d.ReadOnly = true;

            std::vector<BYTE> buf;
            d.Style = L"RAW";
            if (!ReadLayout(h.h, buf)) return true;
            const DRIVE_LAYOUT_INFORMATION_EX* l = (const DRIVE_LAYOUT_INFORMATION_EX*)buf.data();
            if (l->PartitionStyle == PARTITION_STYLE_RAW) return true;
            bool gpt = l->PartitionStyle == PARTITION_STYLE_GPT;
            d.Style = gpt ? L"GPT" : L"MBR";
            for (DWORD i = 0; i < l->PartitionCount; i++)
            {
                const PARTITION_INFORMATION_EX& p = l->PartitionEntry[i];
                // unused entries and the containers of extended partitions
                if (p.PartitionNumber == 0 || p.PartitionLength.QuadPart <= 0) continue;
                if (!gpt && (p.Mbr.PartitionType == PARTITION_ENTRY_UNUSED || IsContainerPartition(p.Mbr.PartitionType))) continue;
                Partition x;
                x.Number = (int)p.PartitionNumber;
                x.Offset = p.StartingOffset.QuadPart;
                x.Length = p.PartitionLength.QuadPart;
                if (gpt) x.GptType = GuidText(p.Gpt.PartitionType);
                else
                {
                    x.MbrType = p.Mbr.PartitionType;
                    x.Active = p.Mbr.BootIndicator != FALSE;
                }
                d.Partitions.push_back(x);
            }
            std::sort(d.Partitions.begin(), d.Partitions.end(),
                [](const Partition& a, const Partition& b) { return a.Number < b.Number; });
            return true;
        }

        std::vector<Disk> Disks()
        {
            std::vector<Disk> r;
            for (int n = 0; n < 128; n++)
            {
                Disk d;
                if (ReadDisk(n, d)) r.push_back(d);
            }
            return r;
        }

        bool SystemPartition(Extent& e)
        {
            wchar_t dev[MAX_PATH] = {};
            DWORD size = sizeof(dev) - sizeof(wchar_t);
            if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", L"SystemPartition", RRF_RT_REG_SZ, nullptr, dev, &size) != ERROR_SUCCESS)
                return false;
            Handle h(OpenDevice(L"\\\\?\\GLOBALROOT" + wstring(dev), 0));
            if (!h.ok()) return false;
            std::vector<Extent> x = ExtentsOf(h.h);
            if (x.empty()) return false;
            e = x[0];
            return true;
        }

        std::vector<int> WindowsDisks()
        {
            std::vector<int> r;
            wstring root = Root(WindowsDir());
            if (root.empty()) return r;
            Handle h(OpenDevice(L"\\\\.\\" + root.substr(0, 2), 0));
            if (!h.ok()) return r;
            for (const Extent& e : ExtentsOf(h.h)) r.push_back(e.Disk);
            return r;
        }

        // ---------- clear and initialize (elevated) ----------

        // writes zeros at [offset, offset + size) of an open disk
        static bool Zero(HANDLE h, long long offset, DWORD size)
        {
            void* z = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!z) return false;
            LARGE_INTEGER at;
            at.QuadPart = offset;
            DWORD n = 0;
            bool ok = SetFilePointerEx(h, at, nullptr, FILE_BEGIN) && WriteFile(h, z, size, &n, nullptr) && n == size;
            DWORD e = GetLastError();
            VirtualFree(z, 0, MEM_RELEASE);
            SetLastError(e);
            return ok;
        }

        DWORD ClearAndInitialize(int disk, bool gpt, wstring& step)
        {
            // the volumes of the disk: locked (or at least dismounted) while their partitions go
            std::vector<Handle> locked;
            for (const Volume& v : Volumes())
            {
                bool on = false;
                for (const Extent& e : v.Extents) if (e.Disk == disk) on = true;
                if (!on) continue;
                Handle h(OpenDevice(NoSlash(v.Path), GENERIC_READ | GENERIC_WRITE));
                if (!h.ok()) continue;
                Ioctl(h.h, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0);
                Ioctl(h.h, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0);
                locked.push_back(std::move(h));
            }

            step = L"open disk " + Num(disk);
            Handle h(OpenDevice(DiskPath(disk), GENERIC_READ | GENERIC_WRITE));
            if (!h.ok()) return GetLastError();
            BYTE geo[sizeof(DISK_GEOMETRY_EX) + 256] = {};
            step = L"read the size of disk " + Num(disk);
            if (!Ioctl(h.h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, geo, sizeof(geo))) return GetLastError();
            long long size = ((const DISK_GEOMETRY_EX*)geo)->DiskSize.QuadPart;
            DWORD sector = ((const DISK_GEOMETRY_EX*)geo)->Geometry.BytesPerSector;
            if (sector == 0) sector = 512;

            // as diskpart "clean": the partition tables go, and the first and
            // last MB of the disk (MBR / protective MBR, both GPT headers) are zeroed
            Ioctl(h.h, IOCTL_DISK_DELETE_DRIVE_LAYOUT, nullptr, 0, nullptr, 0);
            const long long mb = 1048576;
            DWORD head = (DWORD)std::min<long long>(mb, size);
            step = L"clear the start of disk " + Num(disk);
            if (!Zero(h.h, 0, head)) return GetLastError();
            if (size > 2 * mb)
            {
                long long tail = (size - mb) / sector * sector;
                step = L"clear the end of disk " + Num(disk);
                if (!Zero(h.h, tail, (DWORD)(size - tail))) return GetLastError();
            }
            Ioctl(h.h, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0);

            CREATE_DISK cd = {};
            GUID g = {};
            CoCreateGuid(&g);
            if (gpt)
            {
                cd.PartitionStyle = PARTITION_STYLE_GPT;
                cd.Gpt.DiskId = g;
                cd.Gpt.MaxPartitionCount = 128;
            }
            else
            {
                cd.PartitionStyle = PARTITION_STYLE_MBR;
                cd.Mbr.Signature = g.Data1 ? g.Data1 : 0x7A57AB1E;
            }
            step = L"initialize disk " + Num(disk) + (gpt ? L" as GPT" : L" as MBR");
            if (!Ioctl(h.h, IOCTL_DISK_CREATE_DISK, &cd, sizeof(cd), nullptr, 0)) return GetLastError();
            Ioctl(h.h, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0);
            step.clear();
            return 0;
        }

        // ---------- format (fmifs.dll, as format.com) ----------

        enum
        {
            FmPercent = 0, FmIncompatibleFs = 3, FmIncompatibleMedia = 5, FmAccessDenied = 6, FmWriteProtected = 7,
            FmCantLock = 8, FmCantQuickFormat = 9, FmIoError = 10, FmFinished = 11, FmBadLabel = 12
        };
        typedef BOOLEAN(__stdcall* FmCallback)(int type, DWORD length, void* data);
        typedef void(__stdcall* FormatExFn)(const wchar_t* root, DWORD media, const wchar_t* fs, const wchar_t* label,
            BOOL quick, DWORD cluster, FmCallback callback);
        static const DWORD FmHardDisk = 0x0C;

        static bool fmDone, fmOk;
        static int fmProblem;

        static BOOLEAN __stdcall FmProgress(int type, DWORD, void* data)
        {
            switch (type)
            {
            case FmFinished:
                fmDone = true;
                fmOk = data && *(BOOLEAN*)data;
                break;
            case FmIncompatibleFs: case FmIncompatibleMedia: case FmAccessDenied: case FmWriteProtected:
            case FmCantLock: case FmCantQuickFormat: case FmIoError: case FmBadLabel:
                if (fmProblem < 0) fmProblem = type;
                break;
            }
            return TRUE;
        }

        static wstring FmText(int problem)
        {
            switch (problem)
            {
            case FmIncompatibleFs: return L"the file system does not fit the volume";
            case FmIncompatibleMedia: return L"the medium does not fit";
            case FmAccessDenied: return L"access denied";
            case FmWriteProtected: return L"the medium is write-protected";
            case FmCantLock: return L"the volume is in use (it could not be locked)";
            case FmCantQuickFormat: return L"no quick format possible";
            case FmIoError: return L"I/O error";
            case FmBadLabel: return L"the label is not valid";
            }
            return L"the format did not complete";
        }

        // closes what is open on the volume (as Format-Volume -Force)
        static void Dismount(wchar_t letter)
        {
            Handle h(OpenDevice(L"\\\\.\\" + wstring(1, letter) + L":", GENERIC_READ | GENERIC_WRITE));
            if (!h.ok()) return;
            Ioctl(h.h, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0);
            Ioctl(h.h, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0);
        }

        DWORD Format(wchar_t letter, const wstring& fileSystem, const wstring& label, wstring& reason)
        {
            HMODULE m = LoadLibraryExW(L"fmifs.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!m) { reason = L"fmifs.dll: " + ErrorMessage(GetLastError()); return GetLastError(); }
            FormatExFn formatEx = (FormatExFn)GetProcAddress(m, "FormatEx");
            if (!formatEx) { reason = L"FormatEx is missing in fmifs.dll"; FreeLibrary(m); return ERROR_PROC_NOT_FOUND; }
            wstring root = wstring(1, letter) + L":\\";
            for (int attempt = 0; attempt < 2; attempt++)
            {
                fmDone = fmOk = false;
                fmProblem = -1;
                formatEx(root.c_str(), FmHardDisk, fileSystem.c_str(), label.c_str(), TRUE, 0, FmProgress);
                if (fmDone && fmOk) { FreeLibrary(m); reason.clear(); return 0; }
                // something had the volume open: dismount it and try once more
                if (attempt == 0 && fmProblem == FmCantLock) { Dismount(letter); continue; }
                break;
            }
            FreeLibrary(m);
            reason = FmText(fmProblem);
            switch (fmProblem)
            {
            case FmAccessDenied: return ERROR_ACCESS_DENIED;
            case FmWriteProtected: return ERROR_WRITE_PROTECT;
            case FmCantLock: return ERROR_SHARING_VIOLATION;
            case FmBadLabel: return ERROR_LABEL_TOO_LONG;
            }
            return ERROR_GEN_FAILURE;
        }

        // ---------- drive letters ----------

        DWORD AddLetter(int disk, long long offset, wchar_t letter)
        {
            for (const Volume& v : Volumes())
                for (const Extent& e : v.Extents)
                    if (e.Disk == disk && e.Offset == offset)
                    {
                        wstring at = wstring(1, letter) + L":\\";
                        return SetVolumeMountPointW(at.c_str(), v.Path.c_str()) ? 0 : GetLastError();
                    }
            return ERROR_NOT_FOUND;
        }

        DWORD RemoveLetter(wchar_t letter)
        {
            wstring at = wstring(1, letter) + L":\\";
            return DeleteVolumeMountPointW(at.c_str()) ? 0 : GetLastError();
        }

        // ---------- active partition (MBR) ----------

        DWORD SetActive(int disk, int partition)
        {
            Handle h(OpenDevice(DiskPath(disk), GENERIC_READ | GENERIC_WRITE));
            if (!h.ok()) return GetLastError();
            std::vector<BYTE> buf;
            if (!ReadLayout(h.h, buf)) return GetLastError();
            DRIVE_LAYOUT_INFORMATION_EX* l = (DRIVE_LAYOUT_INFORMATION_EX*)buf.data();
            if (l->PartitionStyle != PARTITION_STYLE_MBR) return ERROR_NOT_SUPPORTED;
            bool found = false;
            for (DWORD i = 0; i < l->PartitionCount; i++)
            {
                PARTITION_INFORMATION_EX& p = l->PartitionEntry[i];
                bool me = (int)p.PartitionNumber == partition && p.Mbr.PartitionType != PARTITION_ENTRY_UNUSED;
                if (me) found = true;
                if (p.Mbr.PartitionType != PARTITION_ENTRY_UNUSED && (p.Mbr.BootIndicator != FALSE) != me)
                {
                    p.Mbr.BootIndicator = me ? TRUE : FALSE;
                    p.RewritePartition = TRUE;
                }
            }
            if (!found) return ERROR_NOT_FOUND;
            DWORD size = (DWORD)(offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + l->PartitionCount * sizeof(PARTITION_INFORMATION_EX));
            if (!Ioctl(h.h, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, l, size, nullptr, 0)) return GetLastError();
            Ioctl(h.h, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0);
            return 0;
        }
    }
}
