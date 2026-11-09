// ============================================================
//  WimSmith - libs.h
//  The third-party program in <exe folder>\lib: libwim-15.dll, wimlib (x64).
//  Migrate moves a libwim-15.dll of the old places (pe_tools\, the exe
//  folder) into lib\. When it is missing, EnsureWimlib downloads the
//  newest wimlib-<ver>-windows-x86_64-bin.zip linked on https://wimlib.net/
//  (with a small progress window) and takes libwim-15.dll and the COPYING
//  files out of it with unzip.cpp (no other program). lib\versions.ini
//  records the version. FetchMissingLicenses gets only the license texts,
//  in the background, for a DLL that is there without them.
//  HTTP: WinHTTP of Windows.
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    namespace libs
    {
        wstring Dir(const wstring& root);
        bool HasWimlib(const wstring& root);
        void Migrate(const wstring& root);
        // "" = ready, else the reason (a window shows the download)
        wstring EnsureWimlib(const wstring& root);
        void FetchMissingLicenses(const wstring& root);
        // the version of wimlib in lib\versions.ini ("" = not recorded)
        wstring WimlibVersion(const wstring& root);
    }
}
