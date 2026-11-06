// ============================================================
//  7z_wimlib (C++) - libs.h
//  The third-party programs in <exe folder>\lib (as the C# Libs.cs):
//    libwim-15.dll                 wimlib (x64)
//    7z.exe, 7z.dll, 7zFM.exe      7-Zip (x64), the browse tab
//  Migrate moves a libwim-15.dll of the old places (pe_tools\, the exe
//  folder) into lib\. A missing program is downloaded in its newest
//  version (EnsureWimlib / Ensure7zip, with a small progress window):
//    wimlib : the newest wimlib-<ver>-windows-x86_64-bin.zip linked on
//             https://wimlib.net/ (libwim-15.dll and the COPYING files);
//             the zip is unpacked with lib\7z.exe (7-Zip is fetched
//             first when it is missing) or else Windows' tar.exe
//    7-Zip  : the newest 7z<ver>-x64.exe linked on
//             https://www.7-zip.org/download.html (else the latest
//             GitHub release of ip7z/7zip); the installer is unpacked
//             with 7zr.exe of the same place, nothing is installed.
//  lib\versions.ini records the downloaded versions. The license texts
//  go next to the programs; FetchMissingLicenses gets only the texts, in
//  the background, for a program that is there without them.
//  HTTP: WinHTTP of Windows.
// ============================================================
#pragma once
#include "common.h"

namespace dvt
{
    namespace libs
    {
        wstring Dir(const wstring& root);
        bool HasWimlib(const wstring& root);
        bool Has7zip(const wstring& root);
        void Migrate(const wstring& root);
        // "" = ready, else the reason (a window shows the download)
        wstring EnsureWimlib(const wstring& root);
        wstring Ensure7zip(const wstring& root);
        void FetchMissingLicenses(const wstring& root);
    }
}
