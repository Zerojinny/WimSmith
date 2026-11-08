// ============================================================
//  WimSmith - wim.h
//  wimlib, loaded at run time from <lib>\libwim-15.dll (as the C#
//  version did): it stays a separate, replaceable DLL under its own
//  license (LGPL), and WimSmith starts without it.
//  Only the functions and constants that WimSmith uses are declared
//  here (wimlib.h of wimlib 1.14, x64); structures are read by offset.
// ============================================================
#pragma once
#include "common.h"

struct WIMStruct;

namespace wimsmith
{
    namespace wl
    {
        typedef int(__cdecl* ProgressFunc)(int msg, void* info, void* ctx);
        // the callback of wimlib_iterate_dir_tree: a struct wimlib_dir_entry (read by offset)
        typedef int(__cdecl* DirTreeFunc)(const void* dentry, void* ctx);

#define WL_FUNCS(X) \
        X(int, wimlib_global_init, (int)) \
        X(int, wimlib_create_new_wim, (int, WIMStruct**)) \
        X(int, wimlib_open_wim_with_progress, (const wchar_t*, int, WIMStruct**, ProgressFunc, void*)) \
        X(void, wimlib_register_progress_function, (WIMStruct*, ProgressFunc, void*)) \
        X(int, wimlib_add_image, (WIMStruct*, const wchar_t*, const wchar_t*, const wchar_t*, int)) \
        X(int, wimlib_write, (WIMStruct*, const wchar_t*, int, int, unsigned)) \
        X(int, wimlib_overwrite, (WIMStruct*, int, unsigned)) \
        X(int, wimlib_extract_image, (WIMStruct*, int, const wchar_t*, int)) \
        X(int, wimlib_extract_paths, (WIMStruct*, int, const wchar_t*, const wchar_t* const*, size_t, int)) \
        X(int, wimlib_iterate_dir_tree, (WIMStruct*, int, const wchar_t*, int, DirTreeFunc, void*)) \
        X(int, wimlib_update_image, (WIMStruct*, int, const void*, size_t, int)) \
        X(int, wimlib_delete_image, (WIMStruct*, int)) \
        X(int, wimlib_set_wim_info, (WIMStruct*, const void*, int)) \
        X(int, wimlib_reference_template_image, (WIMStruct*, int, WIMStruct*, int, int)) \
        X(const wchar_t*, wimlib_get_image_property, (const WIMStruct*, int, const wchar_t*)) \
        X(int, wimlib_set_image_property, (WIMStruct*, int, const wchar_t*, const wchar_t*)) \
        X(int, wimlib_get_wim_info, (WIMStruct*, void*)) \
        X(int, wimlib_export_image, (WIMStruct*, int, WIMStruct*, const wchar_t*, const wchar_t*, int)) \
        X(int, wimlib_split, (WIMStruct*, const wchar_t*, uint64_t, int)) \
        X(int, wimlib_reference_resource_files, (WIMStruct*, const wchar_t* const*, unsigned, int, int)) \
        X(int, wimlib_verify_wim, (WIMStruct*, int)) \
        X(int, wimlib_join_with_progress, (const wchar_t* const*, unsigned, const wchar_t*, int, int, ProgressFunc, void*)) \
        X(const wchar_t*, wimlib_get_error_string, (int)) \
        X(void, wimlib_free, (WIMStruct*)) \
        X(int, wimlib_set_error_file_by_name, (const wchar_t*))

#define WL_DECLARE(ret, name, params) extern ret(__cdecl* name) params;
        WL_FUNCS(WL_DECLARE)
#undef WL_DECLARE

        const wchar_t* const DllName = L"libwim-15.dll";

        const int CompressionXpress = 1, CompressionLzx = 2, CompressionLzms = 3;
        const int InitDontAcquirePrivileges = 0x00000002;
        const int OpenCheckIntegrity = 0x00000001, OpenWriteAccess = 0x00000004;
        const int AddVerbose = 0x00000004, AddWinConfig = 0x00000800;
        const int AddSnapshot = 0x00008000;   // VSS snapshot (wimlib 1.9+; administrator, 64-bit)
        const int WriteCheckIntegrity = 0x00000001;   // --check: an integrity table (SHA-1 of every 10 MB)
        const int WriteSolid = 0x00001000;
        // the whole file written again, without the data nothing uses any more
        // (wimlib_overwrite does this by itself after wimlib_delete_image)
        const int WriteRebuild = 0x00000040, WriteRecompress = 0x00000010;
        const int ChangeBootIndex = 0x00000004;   // wimlib_set_wim_info: boot_index (offset 20)
        const int ExportBoot = 0x00000001;
        const int AllImages = -1;
        const int MsgExtractFileStructure = 3, MsgExtractStreams = 4, MsgExtractMetadata = 6;
        const int MsgScanDentry = 10, MsgWriteStreams = 12, MsgWriteMetadataBegin = 13;
        const int MsgVerifyIntegrity = 16, MsgCalcIntegrity = 17, MsgSplitBeginPart = 19, MsgSplitEndPart = 20;
        const int MsgBeginVerifyImage = 27, MsgVerifyStreams = 29;
        const int StatusContinue = 0, StatusAbort = 1;
        // wimlib_iterate_dir_tree / wimlib_extract_paths (browse tab)
        const int IterateRecursive = 0x00000001, IterateChildren = 0x00000002;
        const int ExtractNoAcls = 0x00000040, ExtractReplaceInvalidNames = 0x00000800, ExtractNoPreserveDirs = 0x00200000;
        // restore with the "System Compression" of Windows 10 (Compact OS, wof.sys)
        const int ExtractCompactXpress4k = 0x01000000, ExtractCompactXpress8k = 0x02000000;
        const int ExtractCompactXpress16k = 0x04000000, ExtractCompactLzx = 0x08000000;
        // struct wimlib_dir_entry, x64: full_path (16), attributes (48), last_write_time.tv_sec (88),
        // streams[0].resource.uncompressed_size (248 + 8: the unnamed data stream)
        const int DirEntryFullPath = 16, DirEntryAttributes = 48, DirEntryWriteTime = 88, DirEntrySize = 256;
        // wimlib_update_image (browse tab): struct wimlib_update_command, x64, 40 bytes:
        // op (0), then add { fs_source_path (8), wim_target_path (16), config_file (24), add_flags (32) },
        // delete { wim_path (8), delete_flags (16) } or rename { wim_source_path (8), wim_target_path (16), rename_flags (24) }
        const int UpdateAdd = 0, UpdateDelete = 1, UpdateRename = 2, UpdateCommandSize = 40;
        const int AddNoAcls = 0x00000020, DeleteRecursive = 0x00000002;
        const int ErrAborted = 76, ErrOpen = 47;
        const int ErrPartsMissing = -2;   // a missing part of a split WIM: not a wimlib code

        // loads <toolDir>\libwim-15.dll and initializes wimlib; "" = ready, else the reason
        wstring Load(const wstring& toolDir, bool acquirePrivileges);
        wstring ErrorText(int code);
        // wimlib's own messages in the GUI process (which file, which Windows
        // error): a new file in %TEMP% for the next calls; Messages reads its
        // last lines ("" = none). The file before is deleted.
        wstring BeginMessages();
        // one command of wimlib_update_image: add (a = file or folder on the disk,
        // b = path in the image), delete (a = path in the image) or rename (a -> b)
        struct UpdateCommand
        {
            int op = 0;
            wstring a, b;
            int flags = 0;
        };
        // the commands on an image (struct wimlib_update_command built by offset); 0 or the wimlib code
        int UpdateImage(WIMStruct* wim, int image, const std::vector<UpdateCommand>& cmds);
        wstring Messages(const wstring& file);
        wstring Property(const WIMStruct* wim, int image, const wchar_t* name);

        // struct wimlib_wim_info (read by offset)
        struct WimInfo
        {
            unsigned char b[256];
            bool Read(WIMStruct* wim);
            int ImageCount() const;   // 16
            int BootIndex() const;    // 20
            int PartNumber() const;   // 32 (16 bit)
            int TotalParts() const;   // 34 (16 bit)
            bool Integrity() const;   // bit 0 of the flags at 48
        };

        // a WIMStruct freed at the end of its scope
        struct Wim
        {
            WIMStruct* p = nullptr;
            Wim() {}
            ~Wim() { Reset(); }
            void Reset() { if (p) { wimlib_free(p); p = nullptr; } }
            Wim(const Wim&) = delete;
            Wim& operator=(const Wim&) = delete;
        };

        // opens a WIM file to read it; a part of a split WIM (any part) is
        // opened as its first part and the others are referenced (helper.cpp).
        // total / found: the parts (rc = ErrPartsMissing when one is missing)
        int OpenRead(const wstring& file, int openFlags, ProgressFunc pf, Wim& wim, int& total, int& found);

        // an image of a WIM file (wimread.cpp)
        struct WimImage
        {
            int Index = 0;
            wstring Name, Description;
            long long TotalBytes = -1;
            // filled by ReadInfo only
            wstring Flags, DisplayName, ProductName, EditionId, InstallationType;
            wstring Arch, Version, DefaultLanguage, Languages;
            long long DirCount = -1, FileCount = -1;
            long long Created = 0, Modified = 0;      // FILETIME (UTC), 0 = unknown
        };

        // the header of a WIM file (struct wimlib_wim_info) and its images
        struct WimFileInfo
        {
            wstring Guid;
            int ImageCount = 0, BootIndex = 0, ChunkSize = 0, PartNumber = 0, TotalParts = 0, Compression = 0;
            unsigned Version = 0;
            long long TotalBytes = 0, FileBytes = -1;
            bool Integrity = false, ReadOnlyFlag = false, Pipable = false, Spanned = false;
            std::vector<WimImage> Images;
            bool Solid() const { return Version == 0xE00; }   // solid resources (ESD)
        };
        // the header and the image details (no parts referenced); 0 or the wimlib code
        int ReadInfo(const wstring& wimFile, WimFileInfo& info);
        // the images (index, name, size), read in the GUI process; 0 or the
        // wimlib code (ErrPartsMissing: total / found tell the parts)
        int ReadImages(const wstring& wimFile, std::vector<WimImage>& list, int& total, int& found);
    }
}
