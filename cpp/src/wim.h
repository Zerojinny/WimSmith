// ============================================================
//  7z_wimlib (C++) - wim.h
//  wimlib, loaded at run time from <lib>\libwim-15.dll (as the C#
//  version did): it stays a separate, replaceable DLL under its own
//  license (LGPL), and 7z_wimlib starts without it.
//  Only the functions and constants that 7z_wimlib uses are declared
//  here (wimlib.h of wimlib 1.14, x64); structures are read by offset.
// ============================================================
#pragma once
#include "common.h"

struct WIMStruct;

namespace dvt
{
    namespace wl
    {
        typedef int(__cdecl* ProgressFunc)(int msg, void* info, void* ctx);

#define DVT_WIMLIB_FUNCS(X) \
        X(int, wimlib_global_init, (int)) \
        X(int, wimlib_create_new_wim, (int, WIMStruct**)) \
        X(int, wimlib_open_wim_with_progress, (const wchar_t*, int, WIMStruct**, ProgressFunc, void*)) \
        X(void, wimlib_register_progress_function, (WIMStruct*, ProgressFunc, void*)) \
        X(int, wimlib_add_image, (WIMStruct*, const wchar_t*, const wchar_t*, const wchar_t*, int)) \
        X(int, wimlib_write, (WIMStruct*, const wchar_t*, int, int, unsigned)) \
        X(int, wimlib_overwrite, (WIMStruct*, int, unsigned)) \
        X(int, wimlib_extract_image, (WIMStruct*, int, const wchar_t*, int)) \
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

#define DVT_WIMLIB_DECLARE(ret, name, params) extern ret(__cdecl* name) params;
        DVT_WIMLIB_FUNCS(DVT_WIMLIB_DECLARE)
#undef DVT_WIMLIB_DECLARE

        const wchar_t* const DllName = L"libwim-15.dll";

        const int CompressionXpress = 1, CompressionLzx = 2, CompressionLzms = 3;
        const int InitDontAcquirePrivileges = 0x00000002;
        const int OpenCheckIntegrity = 0x00000001, OpenWriteAccess = 0x00000004;
        const int AddVerbose = 0x00000004, AddWinConfig = 0x00000800;
        const int AddSnapshot = 0x00008000;   // VSS snapshot (wimlib 1.9+; administrator, 64-bit)
        const int WriteSolid = 0x00001000;
        const int ExportBoot = 0x00000001;
        const int AllImages = -1;
        const int MsgExtractFileStructure = 3, MsgExtractStreams = 4, MsgExtractMetadata = 6;
        const int MsgScanDentry = 10, MsgWriteStreams = 12, MsgWriteMetadataBegin = 13;
        const int MsgVerifyIntegrity = 16, MsgSplitBeginPart = 19, MsgSplitEndPart = 20;
        const int MsgBeginVerifyImage = 27, MsgVerifyStreams = 29;
        const int StatusContinue = 0, StatusAbort = 1;
        const int ErrAborted = 76;
        const int ErrPartsMissing = -2;   // a missing part of a split WIM: not a wimlib code

        // loads <toolDir>\libwim-15.dll and initializes wimlib; "" = ready, else the reason
        wstring Load(const wstring& toolDir, bool acquirePrivileges);
        wstring ErrorText(int code);
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
        };
        // the images (index, name, size), read in the GUI process; 0 or the
        // wimlib code (ErrPartsMissing: total / found tell the parts)
        int ReadImages(const wstring& wimFile, std::vector<WimImage>& list, int& total, int& found);
    }
}
